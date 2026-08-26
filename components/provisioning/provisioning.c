/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file provisioning.c
 * @brief Provisioning state machine (docs/PLAN.md section 4).
 *
 *   IDLE -> CONNECTING (saved config) -> probe -> IDLE
 *        -> SCANNING (SmartConfig 60 s) -> softAP fallback (3 min)
 * The state machine is single-instance; prov_poll() drives timeouts and the
 * network probe from net_task.
 */

#include <string.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_smartconfig.h"
#include "esp_http_client.h"
#include "nvs.h"
#include "lwip/ip4_addr.h"
#include "prov_internal.h"

#define TAG "prov"

static prov_state_t s_state;
static prov_event_cb_t s_cb;
static esp_netif_t *s_netif_sta;
static esp_netif_t *s_netif_ap;
static bool s_provision_session;
static int64_t s_state_start_us;
static uint8_t s_probe_retries;
static int64_t s_next_probe_us;
static bool s_pending_probe;
static bool s_configured;                /* NVS 中存在有效 WiFi 配置 */
static bool s_connected;                 /* STA 已获得 IP */
static int64_t s_next_reconnect_us;      /* 0 = 未安排重连 */
static uint32_t s_reconnect_delay_ms;    /* 指数退避当前档位 */

static esp_err_t s_fail_and_enter_smartconfig(void);
static void s_wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data);
static void s_ip_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data);
static void s_sc_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data);

/* 指数退避：返回本次等待时长，并翻倍下一档（封顶 CONFIG_LERO_PROV_RECONNECT_MAX_MS） */
static uint32_t s_reconnect_delay(void)
{
    uint32_t delay = s_reconnect_delay_ms;
    if (delay < (uint32_t)CONFIG_LERO_PROV_RECONNECT_MIN_MS) {
        delay = (uint32_t)CONFIG_LERO_PROV_RECONNECT_MIN_MS;
    }
    s_reconnect_delay_ms = (delay >= (uint32_t)CONFIG_LERO_PROV_RECONNECT_MAX_MS)
                               ? (uint32_t)CONFIG_LERO_PROV_RECONNECT_MAX_MS
                               : (delay * 2U);
    return delay;
}

static void s_try_reconnect(void)
{
    wifi_mode_t mode = WIFI_MODE_NULL;
    if ((esp_wifi_get_mode(&mode) != ESP_OK) || (mode != WIFI_MODE_STA)) {
        (void)prov_wifi_start_sta();    /* softAP 停止后 wifi 未启动，先回 STA */
    }
    const esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "reconnect failed: %s", esp_err_to_name(err));
        s_next_reconnect_us = esp_timer_get_time() + (int64_t)s_reconnect_delay() * 1000;
    }
}

/* ------------------------------------------------------------------------- */
/* State helpers                                                             */
/* ------------------------------------------------------------------------- */

void prov_set_state(prov_state_t state, const char *detail)
{
    s_state = state;
    s_state_start_us = esp_timer_get_time();
    ESP_LOGI(TAG, "state=%d (%s)", (int)state, (detail != NULL) ? detail : "-");
    if (s_cb != NULL) {
        s_cb(state, detail);
    }
}

void prov_notify(const char *detail)
{
    if (s_cb != NULL) {
        s_cb(s_state, detail);
    }
}

bool prov_is_provision_session(void)
{
    return s_provision_session;
}

void prov_set_provision_session(bool on)
{
    s_provision_session = on;
}

/* ------------------------------------------------------------------------- */
/* WiFi helpers                                                              */
/* ------------------------------------------------------------------------- */

esp_err_t prov_wifi_set_mode_start(wifi_mode_t mode)
{
    /* 切换模式前先 stop（未启动时忽略错误），保证 set_mode 生效 */
    esp_err_t err = esp_wifi_stop();
    if (err == ESP_ERR_WIFI_NOT_STARTED) {
        err = ESP_OK;
    }
    if (err == ESP_OK) {
        err = esp_wifi_set_mode(mode);
    }
    if (err == ESP_OK) {
        err = esp_wifi_start();
    }
    return err;
}

esp_err_t prov_wifi_start_sta(void)
{
    return prov_wifi_set_mode_start(WIFI_MODE_STA);
}

esp_err_t prov_wifi_connect_sta(const char *ssid, const char *pwd)
{
    if ((ssid == NULL) || (pwd == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    (void)esp_wifi_set_storage(WIFI_STORAGE_RAM);

    wifi_config_t cfg;
    (void)memset(&cfg, 0, sizeof(cfg));
    (void)strlcpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid));
    (void)strlcpy((char *)cfg.sta.password, pwd, sizeof(cfg.sta.password));
    cfg.sta.threshold.authmode = (pwd[0] != '\0') ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    cfg.sta.pmf_cfg.capable = true;
    cfg.sta.pmf_cfg.required = false;

    /* 已在 STA 模式且已启动（如 SmartConfig 阶段）时不重启 wifi，
     * 避免打断配网尾巴 ACK / 丢归属事件。 */
    wifi_mode_t mode = WIFI_MODE_NULL;
    esp_err_t err = esp_wifi_get_mode(&mode);
    if ((err != ESP_OK) || (mode != WIFI_MODE_STA)) {
        err = prov_wifi_set_mode_start(WIFI_MODE_STA);
    }
    if (err == ESP_OK) {
        err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    }
    if (err == ESP_OK) {
        err = esp_wifi_connect();
    }
    return err;
}

esp_err_t prov_wifi_disconnect(void)
{
    esp_err_t err = esp_wifi_disconnect();
    if (err == ESP_ERR_WIFI_NOT_STARTED) {
        err = ESP_OK;
    }
    return err;
}

bool prov_wifi_is_connected(void)
{
    wifi_ap_record_t info;
    (void)memset(&info, 0, sizeof(info));
    return (esp_wifi_sta_get_ap_info(&info) == ESP_OK);
}

esp_err_t prov_get_sta_ip(char *ip, size_t ip_len)
{
    if ((ip == NULL) || (ip_len == 0U) || (s_netif_sta == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_netif_ip_info_t info;
    esp_err_t err = esp_netif_get_ip_info(s_netif_sta, &info);
    if (err != ESP_OK) {
        return err;
    }
    (void)snprintf(ip, ip_len, IPSTR, IP2STR(&info.ip));
    return ESP_OK;
}

esp_err_t prov_get_sta_rssi(int8_t *rssi)
{
    if (rssi == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    wifi_ap_record_t info;
    (void)memset(&info, 0, sizeof(info));
    esp_err_t err = esp_wifi_sta_get_ap_info(&info);
    if (err != ESP_OK) {
        return err;
    }
    *rssi = info.rssi;
    return ESP_OK;
}

/* ------------------------------------------------------------------------- */
/* Network probe                                                             */
/* ------------------------------------------------------------------------- */

esp_err_t prov_probe_network(void)
{
    esp_http_client_config_t cfg = {
        .url = CONFIG_LERO_PROV_PROBE_URL,
        .method = HTTP_METHOD_GET,
        .timeout_ms = CONFIG_LERO_PROV_PROBE_TIMEOUT_MS,
        .buffer_size = 512,
        .disable_auto_redirect = false,
        .keep_alive_enable = false,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        ESP_LOGE(TAG, "http client init failed");
        return ESP_FAIL;
    }
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        const int status = esp_http_client_get_status_code(client);
        if ((status < 200) || (status >= 400)) {
            ESP_LOGW(TAG, "probe http status %d", status);
            err = ESP_FAIL;
        } else {
            ESP_LOGI(TAG, "network probe ok (status %d)", status);
        }
    } else {
        ESP_LOGW(TAG, "network probe failed: %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
    return err;
}

/* ------------------------------------------------------------------------- */
/* Event handlers                                                            */
/* ------------------------------------------------------------------------- */

static void s_wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)data;
    if (base != WIFI_EVENT) {
        return;
    }
    if (id == WIFI_EVENT_STA_START) {
        if (s_state == PROV_STATE_CONNECTING) {
            (void)esp_wifi_connect();
        }
    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        if (s_state == PROV_STATE_SCANNING) {
            return;                 /* SmartConfig 阶段忽略断线 */
        }
        if (s_state == PROV_STATE_CONNECTING) {
            ESP_LOGW(TAG, "wifi connect failed");
            (void)s_fail_and_enter_smartconfig();
            return;
        }
        /* IDLE / DONE：已配置网络的运行期掉线 → 安排自动重连（指数退避） */
        if (s_configured && (s_next_reconnect_us == 0)) {
            ESP_LOGW(TAG, "connection lost, reconnect in %lu ms",
                     (unsigned long)s_reconnect_delay());
            s_next_reconnect_us = esp_timer_get_time() + (int64_t)s_reconnect_delay() * 1000;
        }
    }
}

static void s_ip_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)data;
    if ((base == IP_EVENT) && (id == IP_EVENT_STA_GOT_IP)) {
        /* 事件上下文不阻塞：网络探测推迟到 prov_poll() */
        s_connected = true;
        s_next_reconnect_us = 0;
        s_reconnect_delay_ms = (uint32_t)CONFIG_LERO_PROV_RECONNECT_MIN_MS;
        s_pending_probe = true;
        char ip[16];
        if (prov_get_sta_ip(ip, sizeof(ip)) == ESP_OK) {
            ESP_LOGI(TAG, "got ip %s", ip);
        }
    }
}

static void s_sc_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base != SC_EVENT) {
        return;
    }
    if (id == SC_EVENT_GOT_SSID_PSWD) {
        const smartconfig_event_got_ssid_pswd_t *ev =
            (const smartconfig_event_got_ssid_pswd_t *)data;
        if (ev == NULL) {
            return;
        }
        prov_smartconfig_set_creds((const char *)ev->ssid, (const char *)ev->password);
        char ssid[PROV_SSID_MAX + 1U];
        char pwd[PROV_PWD_MAX + 1U];
        if (prov_smartconfig_get_creds(ssid, sizeof(ssid), pwd, sizeof(pwd)) == ESP_OK) {
            ESP_LOGI(TAG, "smartconfig got ssid/password, connecting");
            (void)prov_wifi_connect_sta(ssid, pwd);
            prov_set_state(PROV_STATE_CONNECTING, "smartconfig creds");
        }
    } else if (id == SC_EVENT_SCAN_DONE) {
        ESP_LOGD(TAG, "smartconfig scan done");
    }
}

/* ------------------------------------------------------------------------- */
/* Internal transitions                                                      */
/* ------------------------------------------------------------------------- */

static esp_err_t s_fail_and_enter_smartconfig(void)
{
    (void)prov_smartconfig_stop();
    (void)prov_nvs_clear_wifi();
    s_configured = false;
    prov_set_state(PROV_STATE_IDLE, "connect failed");
    return prov_enter_smartconfig();
}

/* ------------------------------------------------------------------------- */
/* Public API                                                                */
/* ------------------------------------------------------------------------- */

esp_err_t prov_init(prov_event_cb_t cb)
{
    s_cb = cb;
    esp_err_t err = esp_netif_init();
    if (err == ESP_OK) {
        err = esp_event_loop_create_default();
        if (err == ESP_ERR_INVALID_STATE) {
            err = ESP_OK;           /* 已创建（其他组件先调用） */
        }
    }
    if (err == ESP_OK) {
        s_netif_sta = esp_netif_create_default_wifi_sta();
        s_netif_ap = esp_netif_create_default_wifi_ap();
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        err = esp_wifi_init(&cfg);
    }
    if (err == ESP_OK) {
        err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    }
    if (err == ESP_OK) {
        err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                         s_wifi_event_handler, NULL);
    }
    if (err == ESP_OK) {
        err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                         s_ip_event_handler, NULL);
    }
    if (err == ESP_OK) {
        err = esp_event_handler_register(SC_EVENT, ESP_EVENT_ANY_ID,
                                         s_sc_event_handler, NULL);
    }
    if (err == ESP_OK) {
        prov_set_state(PROV_STATE_IDLE, "init");
    }
    return err;
}

esp_err_t prov_start(void)
{
    char ssid[PROV_SSID_MAX + 1U];
    char pwd[PROV_PWD_MAX + 1U];
    bool configured = false;
    esp_err_t err = prov_nvs_load_wifi(ssid, sizeof(ssid), pwd, sizeof(pwd), &configured);
    if ((err != ESP_OK) && (err != ESP_ERR_NVS_NOT_FOUND)) {
        ESP_LOGE(TAG, "load wifi config failed: %s", esp_err_to_name(err));
    }
    s_configured = configured && (ssid[0] != '\0');
    s_reconnect_delay_ms = (uint32_t)CONFIG_LERO_PROV_RECONNECT_MIN_MS;
    if (configured && (ssid[0] != '\0')) {
        ESP_LOGI(TAG, "saved config found, connecting %s", ssid);
        err = prov_wifi_connect_sta(ssid, pwd);
        if (err == ESP_OK) {
            prov_set_state(PROV_STATE_CONNECTING, "saved config");
        } else {
            err = prov_enter_smartconfig();
        }
    } else {
        err = prov_enter_smartconfig();
    }
    return err;
}

esp_err_t prov_enter_smartconfig(void)
{
    if (s_state == PROV_STATE_SCANNING) {
        return ESP_OK;
    }
    (void)prov_softap_stop();
    (void)prov_wifi_disconnect();

    esp_err_t err = prov_wifi_start_sta();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wifi start failed: %s", esp_err_to_name(err));
        return err;
    }
    s_provision_session = true;
    s_probe_retries = 0U;
    s_pending_probe = false;
    s_next_probe_us = 0;

    err = prov_smartconfig_start();
    if (err == ESP_OK) {
        prov_set_state(PROV_STATE_SCANNING, "smartconfig listening");
    }
    return err;
}

esp_err_t prov_enter_softap(void)
{
    if (s_state == PROV_STATE_AP_FALLBACK) {
        return ESP_OK;
    }
    (void)prov_smartconfig_stop();
    s_provision_session = false;

    esp_err_t err = prov_softap_start();
    if (err == ESP_OK) {
        prov_set_state(PROV_STATE_AP_FALLBACK, "softap fallback");
    }
    return err;
}

esp_err_t prov_stop(void)
{
    (void)prov_smartconfig_stop();
    (void)prov_softap_stop();
    (void)prov_wifi_disconnect();
    s_provision_session = false;
    prov_set_state(PROV_STATE_IDLE, "stopped");
    return ESP_OK;
}

esp_err_t prov_poll(void)
{
    const int64_t now = esp_timer_get_time();
    switch (s_state) {
    case PROV_STATE_CONNECTING: {
        if (s_pending_probe) {
            s_pending_probe = false;
            if (prov_probe_network() == ESP_OK) {
                if (s_provision_session) {
                    char ssid[PROV_SSID_MAX + 1U];
                    char pwd[PROV_PWD_MAX + 1U];
                    if (prov_smartconfig_get_creds(ssid, sizeof(ssid),
                                                    pwd, sizeof(pwd)) == ESP_OK) {
                        (void)prov_nvs_save_wifi(ssid, pwd);
                    }
                    s_configured = true;
                    (void)prov_smartconfig_stop();
                    s_provision_session = false;
                    prov_set_state(PROV_STATE_DONE, "provisioning ok");
                } else {
                    prov_set_state(PROV_STATE_IDLE, "connected");
                }
            } else {
                s_probe_retries++;
                if (s_probe_retries >= (uint8_t)CONFIG_LERO_PROV_PROBE_MAX_RETRIES) {
                    ESP_LOGW(TAG, "probe retries exhausted");
                    (void)s_fail_and_enter_smartconfig();
                } else {
                    s_next_probe_us = now + (int64_t)5000 * 1000;
                }
            }
        } else if (s_next_probe_us != 0) {
            if (now >= s_next_probe_us) {
                s_next_probe_us = 0;
                s_pending_probe = true;
            }
        } else if ((now - s_state_start_us) >
                   ((int64_t)CONFIG_LERO_PROV_WIFI_CONNECT_TIMEOUT_MS * 1000)) {
            ESP_LOGW(TAG, "connect timeout");
            (void)s_fail_and_enter_smartconfig();
        }
        break;
    }
    case PROV_STATE_SCANNING:
        if ((now - s_state_start_us) >
            ((int64_t)CONFIG_LERO_PROV_SMARTCONFIG_TIMEOUT_MS * 1000)) {
            ESP_LOGW(TAG, "smartconfig timeout -> softAP");
            (void)prov_enter_softap();
        }
        break;
    case PROV_STATE_AP_FALLBACK:
        if ((now - s_state_start_us) >
            ((int64_t)CONFIG_LERO_PROV_AP_TIMEOUT_MS * 1000)) {
            ESP_LOGW(TAG, "softAP idle timeout -> idle");
            (void)prov_softap_stop();
            prov_set_state(PROV_STATE_IDLE, "ap timeout");
        }
        break;
    case PROV_STATE_IDLE:
    case PROV_STATE_DONE: {
        if (s_state == PROV_STATE_DONE) {
            /* 配网成功提示期结束后回到 IDLE（连接状态保持） */
            if ((now - s_state_start_us) >
                ((int64_t)CONFIG_LERO_PROV_DONE_IDLE_MS * 1000)) {
                prov_set_state(PROV_STATE_IDLE, "done->idle");
            }
        }
        /* 运行期掉线自动重连（指数退避，见 s_reconnect_delay） */
        if ((s_next_reconnect_us != 0) && (now >= s_next_reconnect_us)) {
            s_next_reconnect_us = 0;
            if (s_connected || prov_wifi_is_connected()) {
                s_connected = true;
            } else {
                s_try_reconnect();
            }
        }
        break;
    }
    default:
        break;
    }
    return ESP_OK;
}

prov_state_t prov_get_state(void)
{
    return s_state;
}

esp_err_t prov_get_wifi_status(prov_wifi_status_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    (void)memset(status, 0, sizeof(*status));
    char ssid[PROV_SSID_MAX + 1U];
    char pwd[PROV_PWD_MAX + 1U];
    bool configured = false;
    esp_err_t err = prov_nvs_load_wifi(ssid, sizeof(ssid), pwd, sizeof(pwd), &configured);
    status->configured = configured;
    if (err != ESP_OK) {
        return err;
    }
    (void)strlcpy(status->ssid, ssid, sizeof(status->ssid));

    wifi_ap_record_t ap;
    (void)memset(&ap, 0, sizeof(ap));
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        status->rssi = ap.rssi;
        (void)strlcpy(status->ssid, (const char *)ap.ssid, sizeof(status->ssid));
    }
    (void)prov_get_sta_ip(status->ip, sizeof(status->ip));
    uint8_t mac[6] = { 0 };
    if (esp_wifi_get_mac(WIFI_IF_STA, mac) == ESP_OK) {
        (void)snprintf(status->mac, sizeof(status->mac),
                       "%02x:%02x:%02x:%02x:%02x:%02x",
                       mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
    return ESP_OK;
}

esp_err_t prov_factory_reset(void)
{
    return prov_nvs_factory_reset();
}


/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "net_wifi.h"

#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "nvs_flash.h"

#define NET_WIFI_NVS_NAMESPACE "wifi_cfg"
#define NET_WIFI_NVS_KEY_SSID  "ssid"
#define NET_WIFI_NVS_KEY_PASS  "pass"
#define NET_WIFI_MAX_RETRY     4

static const char *TAG = "net_wifi";

static SemaphoreHandle_t s_lock;
static volatile net_wifi_state_t s_state = NET_WIFI_IDLE;
static volatile bool s_scan_busy = false;
static int s_retry = 0;
static bool s_connect_requested = false;

static char s_scan_ssids[NET_WIFI_SCAN_MAX][NET_WIFI_SSID_MAX_LEN];
static net_wifi_ap_t s_scan_aps[NET_WIFI_SCAN_MAX];
static int s_scan_count = 0;
static char s_ip[16] = {0};

static void net_wifi_set_state(net_wifi_state_t state)
{
    s_state = state;
}

static void net_wifi_store_scan_results(void)
{
    uint16_t ap_num = 0;
    esp_wifi_scan_get_ap_num(&ap_num);
    if (ap_num > NET_WIFI_SCAN_MAX) {
        ap_num = NET_WIFI_SCAN_MAX;
    }

    static wifi_ap_record_t records[NET_WIFI_SCAN_MAX];
    uint16_t got = NET_WIFI_SCAN_MAX;
    if (esp_wifi_scan_get_ap_records(&got, records) != ESP_OK) {
        got = 0;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_scan_count = 0;
    for (int i = 0; i < got && s_scan_count < NET_WIFI_SCAN_MAX; i++) {
        if (records[i].ssid[0] == '\0') {
            continue;
        }
        bool dup = false;
        for (int j = 0; j < s_scan_count; j++) {
            if (strncmp(s_scan_ssids[j], (const char *)records[i].ssid, NET_WIFI_SSID_MAX_LEN) == 0) {
                dup = true;
                break;
            }
        }
        if (dup) {
            continue;
        }
        strlcpy(s_scan_ssids[s_scan_count], (const char *)records[i].ssid, NET_WIFI_SSID_MAX_LEN);
        strlcpy(s_scan_aps[s_scan_count].ssid, (const char *)records[i].ssid, NET_WIFI_SSID_MAX_LEN);
        s_scan_aps[s_scan_count].rssi = records[i].rssi;
        s_scan_aps[s_scan_count].locked = (records[i].authmode != WIFI_AUTH_OPEN);
        s_scan_count++;
    }
    xSemaphoreGive(s_lock);
    ESP_LOGI(TAG, "Scan stored %d SSIDs", s_scan_count);
}

static void net_wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT) {
        switch (id) {
        case WIFI_EVENT_STA_START:
            if (s_connect_requested) {
                esp_wifi_connect();
            }
            break;
        case WIFI_EVENT_SCAN_DONE:
            net_wifi_store_scan_results();
            s_scan_busy = false;
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            if (s_connect_requested && s_retry < NET_WIFI_MAX_RETRY) {
                s_retry++;
                ESP_LOGW(TAG, "Reconnect attempt %d/%d", s_retry, NET_WIFI_MAX_RETRY);
                esp_wifi_connect();
                net_wifi_set_state(NET_WIFI_CONNECTING);
            } else if (s_connect_requested) {
                ESP_LOGE(TAG, "Connect failed");
                net_wifi_set_state(NET_WIFI_FAILED);
            }
            break;
        default:
            break;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&event->ip_info.ip));
        xSemaphoreGive(s_lock);
        s_retry = 0;
        net_wifi_set_state(NET_WIFI_CONNECTED);
        ESP_LOGI(TAG, "Got IP: %s", s_ip);
    }
}

esp_err_t net_wifi_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        net_wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        net_wifi_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_start());

    net_wifi_set_state(NET_WIFI_IDLE);
    ESP_LOGI(TAG, "Wi-Fi station started");
    return ESP_OK;
}

void net_wifi_scan_start(void)
{
    if (s_scan_busy) {
        return;
    }
    s_scan_busy = true;
    net_wifi_set_state(NET_WIFI_SCANNING);
    wifi_scan_config_t scan_cfg = {
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
    };
    if (esp_wifi_scan_start(&scan_cfg, false) != ESP_OK) {
        s_scan_busy = false;
        ESP_LOGW(TAG, "Scan start failed");
    }
}

bool net_wifi_scan_busy(void)
{
    return s_scan_busy;
}

int net_wifi_scan_get(char ssids[][NET_WIFI_SSID_MAX_LEN], int max_count)
{
    int count;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    count = s_scan_count < max_count ? s_scan_count : max_count;
    for (int i = 0; i < count; i++) {
        strlcpy(ssids[i], s_scan_ssids[i], NET_WIFI_SSID_MAX_LEN);
    }
    xSemaphoreGive(s_lock);
    return count;
}

int net_wifi_scan_get_aps(net_wifi_ap_t *aps, int max_count)
{
    int count;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    count = s_scan_count < max_count ? s_scan_count : max_count;
    for (int i = 0; i < count; i++) {
        aps[i] = s_scan_aps[i];
    }
    xSemaphoreGive(s_lock);
    return count;
}

static void net_wifi_save_creds(const char *ssid, const char *password)
{
    nvs_handle_t handle;
    if (nvs_open(NET_WIFI_NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        return;
    }
    nvs_set_str(handle, NET_WIFI_NVS_KEY_SSID, ssid);
    nvs_set_str(handle, NET_WIFI_NVS_KEY_PASS, password ? password : "");
    nvs_commit(handle);
    nvs_close(handle);
}

esp_err_t net_wifi_connect(const char *ssid, const char *password, bool save)
{
    if (!ssid || ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_config_t wifi_config = {0};
    strlcpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, password ? password : "", sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;

    /* A scan in progress or a live association makes esp_wifi_connect() a
     * no-op, which is why connecting from the UI (after a scan / while already
     * connected from boot) fails. Stop scanning and drop the old link first. */
    esp_wifi_scan_stop();
    s_scan_busy = false;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    s_retry = 0;
    s_connect_requested = true;
    net_wifi_set_state(NET_WIFI_CONNECTING);

    if (save) {
        net_wifi_save_creds(ssid, password);
    }

    /* Drop any current association; the induced STA_DISCONNECTED event will
     * also kick a connect via the event handler, covering both the fresh and
     * the reconnect cases. */
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(120));

    esp_err_t ret = esp_wifi_connect();
    if (ret != ESP_OK && ret != ESP_ERR_WIFI_CONN) {
        ESP_LOGW(TAG, "esp_wifi_connect returned %s", esp_err_to_name(ret));
    }
    ESP_LOGI(TAG, "Connecting to \"%s\"", ssid);
    return ESP_OK;
}

net_wifi_state_t net_wifi_get_state(void)
{
    return s_state;
}

bool net_wifi_get_ip(char *out, size_t len)
{
    bool ok;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    ok = s_ip[0] != '\0';
    if (ok && out) {
        strlcpy(out, s_ip, len);
    }
    xSemaphoreGive(s_lock);
    return ok;
}

bool net_wifi_load_creds(char *ssid, size_t ssid_len, char *pass, size_t pass_len)
{
    nvs_handle_t handle;
    if (nvs_open(NET_WIFI_NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }

    size_t sl = ssid_len;
    size_t pl = pass_len;
    esp_err_t r1 = nvs_get_str(handle, NET_WIFI_NVS_KEY_SSID, ssid, &sl);
    esp_err_t r2 = nvs_get_str(handle, NET_WIFI_NVS_KEY_PASS, pass, &pl);
    nvs_close(handle);

    if (r1 != ESP_OK || ssid[0] == '\0') {
        return false;
    }
    if (r2 != ESP_OK) {
        pass[0] = '\0';
    }
    return true;
}

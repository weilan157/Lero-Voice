/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file prov_softap.c
 * @brief softAP fallback with a tiny HTTP configuration page
 *        (AP: LeroVoice-XXXX, http://192.168.4.1, docs/PLAN.md 4.3 step 4).
 *
 * Raw lwIP sockets (no HTTP server component): GET / serves the form,
 * POST /save persists the credentials and switches to station mode.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "prov_internal.h"

#define TAG "prov_ap"

#define AP_PORT             80
#define REQ_BUF_SIZE        2048U
#define AP_TASK_STACK_BYTES 4096U
#define AP_TASK_PRIORITY    7
#define AP_TASK_CORE        0

static const char s_page_form[] =
    "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>Lero Voice</title>"
    "<style>body{font-family:sans-serif;margin:2em}input{width:90%;padding:.5em}"
    "button{padding:.6em 1.5em}</style></head>"
    "<body><h2>Lero Voice 配网</h2>"
    "<form method=\"post\" action=\"/save\">"
    "<p>WiFi SSID<br><input name=\"ssid\" size=\"32\"></p>"
    "<p>密码<br><input type=\"password\" name=\"password\" size=\"32\"></p>"
    "<p><button type=\"submit\">保存并连接</button></p></form>"
    "<p><small>仅支持 2.4G 网络；企业级 802.1X 不支持</small></p></body></html>";

static const char s_page_saved[] =
    "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>Lero Voice</title>"
    "<style>body{font-family:sans-serif;margin:2em}</style></head>"
    "<body><h2>已保存，正在连接...</h2>"
    "<p>请稍候，设备会自动切换到 STA 模式。</p></body></html>";

static const char s_http_ok[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/html; charset=utf-8\r\n"
    "Connection: close\r\n"
    "\r\n";

static bool s_running;
static int s_listen_fd = -1;
static char s_ap_ssid[32];
static StackType_t s_task_stack[AP_TASK_STACK_BYTES / sizeof(StackType_t)];
static StaticTask_t s_task_tcb;
static volatile bool s_stop_requested;  /* stop：请求 AP 任务退出 */
static volatile bool s_task_active;     /* AP 任务存活（start 前须确认退出） */

static void s_ap_task(void *arg);

static uint8_t s_hex_val(char c)
{
    if ((c >= '0') && (c <= '9')) {
        return (uint8_t)(c - '0');
    }
    if ((c >= 'a') && (c <= 'f')) {
        return (uint8_t)(c - 'a' + 10);
    }
    if ((c >= 'A') && (c <= 'F')) {
        return (uint8_t)(c - 'A' + 10);
    }
    return 0U;
}

static size_t s_url_decode(const char *src, char *dst, size_t dst_size)
{
    size_t di = 0U;
    for (size_t si = 0U; (src[si] != '\0') && (di + 1U < dst_size); si++) {
        const char c = src[si];
        if (c == '%') {
            const char hi = src[si + 1U];
            const char lo = src[si + 2U];
            if ((hi != '\0') && (lo != '\0')) {
                dst[di++] = (char)((s_hex_val(hi) << 4U) | s_hex_val(lo));
                si += 2U;
                continue;
            }
        }
        dst[di++] = (c == '+') ? ' ' : c;
    }
    dst[di] = '\0';
    return di;
}

static void s_extract_field(const char *body, const char *name,
                            char *out, size_t out_size)
{
    out[0] = '\0';
    if ((body == NULL) || (name == NULL) || (out_size == 0U)) {
        return;
    }
    const char *start = strstr(body, name);
    if (start == NULL) {
        return;
    }
    start += strlen(name);
    size_t len = 0U;
    while ((start[len] != '\0') && (start[len] != '&') &&
           (len + 1U < out_size)) {
        len++;
    }
    (void)strlcpy(out, start, len + 1U);
}

static void s_send_all(int fd, const char *buf, size_t len)
{
    size_t sent = 0U;
    while (sent < len) {
        const int n = send(fd, buf + sent, len - sent, 0);
        if (n <= 0) {
            break;
        }
        sent += (size_t)n;
    }
}

static void s_send_page(int fd, const char *page)
{
    s_send_all(fd, s_http_ok, strlen(s_http_ok));
    s_send_all(fd, page, strlen(page));
}

static void s_handle_save(const char *body)
{
    char ssid[PROV_SSID_MAX + 1U];
    char pwd[PROV_PWD_MAX + 1U];
    s_extract_field(body, "ssid=", ssid, sizeof(ssid));
    s_extract_field(body, "password=", pwd, sizeof(pwd));

    size_t ssid_len = s_url_decode(ssid, ssid, sizeof(ssid));
    size_t pwd_len = s_url_decode(pwd, pwd, sizeof(pwd));
    (void)ssid_len;
    (void)pwd_len;

    if (ssid[0] == '\0') {
        ESP_LOGW(TAG, "save request without ssid");
        return;
    }
    esp_err_t err = prov_nvs_save_wifi(ssid, pwd);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "save wifi failed: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "wifi saved, switching to STA");
    prov_softap_stop();
    prov_set_state(PROV_STATE_CONNECTING, "softap saved");
    (void)prov_wifi_connect_sta(ssid, pwd);
}

static void s_handle_request(int fd, const char *req)
{
    if (strncmp(req, "GET ", 4U) == 0) {
        s_send_page(fd, s_page_form);
        return;
    }
    if (strncmp(req, "POST ", 5U) == 0) {
        const char *body = strstr(req, "\r\n\r\n");
        if (body != NULL) {
            body += 4U;
            if (strncmp(req + 5U, "/save", 5U) == 0) {
                s_send_page(fd, s_page_saved);
                s_handle_save(body);
                return;
            }
        }
    }
    /* 未知请求：返回表单兜底 */
    s_send_page(fd, s_page_form);
}

static void s_ap_task(void *arg)
{
    (void)arg;
    for (;;) {
        if (s_stop_requested) {
            break;
        }
        struct sockaddr_in from;
        socklen_t from_len = sizeof(from);
        const int fd = accept(s_listen_fd, (struct sockaddr *)&from, &from_len);
        if (fd < 0) {
            break;                  /* listen socket 已关闭 -> 退出 */
        }
        /* recv 带超时：stop 请求可在阻塞中周期性唤醒任务 */
        struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
        (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        char req[REQ_BUF_SIZE];
        const int n = recv(fd, req, sizeof(req) - 1U, 0);
        if (n > 0) {
            req[n] = '\0';
            s_handle_request(fd, req);
        } else if (s_stop_requested) {
            (void)close(fd);
            break;
        }
        (void)close(fd);
    }
    /* 任务退出路径复位标志（供 start 复用静态 TCB/栈前确认） */
    s_running = false;
    s_task_active = false;
    vTaskDelete(NULL);
}

esp_err_t prov_softap_start(void)
{
    if (s_running) {
        return ESP_OK;
    }
    /* 异常残留任务（非 stop 路径退出失败）：先等其退出再复用静态栈 */
    if (s_task_active) {
        ESP_LOGW(TAG, "ap task still active, waiting for exit");
        for (int i = 0; (i < 300) && s_task_active; i++) {
            vTaskDelay(pdMS_TO_TICKS(10U));
        }
        if (s_task_active) {
            ESP_LOGE(TAG, "ap task failed to exit; abort start");
            return ESP_FAIL;
        }
    }
    uint8_t mac[6] = { 0 };
    (void)esp_wifi_get_mac(WIFI_IF_AP, mac);
    (void)snprintf(s_ap_ssid, sizeof(s_ap_ssid), "%s%02X%02X",
                   CONFIG_LERO_PROV_AP_SSID_PREFIX, mac[4], mac[5]);

    wifi_config_t cfg;
    (void)memset(&cfg, 0, sizeof(cfg));
    (void)strlcpy((char *)cfg.ap.ssid, s_ap_ssid, sizeof(cfg.ap.ssid));
    cfg.ap.ssid_len = (uint8_t)strlen(s_ap_ssid);
    cfg.ap.channel = 1;
    cfg.ap.max_connection = 4;
    cfg.ap.authmode = WIFI_AUTH_OPEN;
    cfg.ap.ssid_hidden = 0;

    esp_err_t err = prov_wifi_set_mode_start(WIFI_MODE_AP);
    if (err == ESP_OK) {
        err = esp_wifi_set_config(WIFI_IF_AP, &cfg);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ap start failed: %s", esp_err_to_name(err));
        return err;
    }

    s_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (s_listen_fd < 0) {
        ESP_LOGE(TAG, "socket create failed");
        return ESP_FAIL;
    }
    int reuse = 1;
    (void)setsockopt(s_listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in addr;
    (void)memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(AP_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    err = bind(s_listen_fd, (struct sockaddr *)&addr, sizeof(addr));
    if (err == 0) {
        err = listen(s_listen_fd, 2);
    }
    if (err != 0) {
        ESP_LOGE(TAG, "bind/listen failed: %d", (int)err);
        (void)close(s_listen_fd);
        s_listen_fd = -1;
        return ESP_FAIL;
    }

    s_running = true;
    s_stop_requested = false;
    TaskHandle_t created = xTaskCreateStaticPinnedToCore(
        s_ap_task, "prov_ap", sizeof(s_task_stack), NULL,
        AP_TASK_PRIORITY, s_task_stack, &s_task_tcb, AP_TASK_CORE);
    if (created == NULL) {
        ESP_LOGE(TAG, "ap task create failed");
        s_running = false;
        s_task_active = false;
        (void)close(s_listen_fd);
        s_listen_fd = -1;
        return ESP_ERR_NO_MEM;
    }
    s_task_active = true;
    ESP_LOGI(TAG, "softAP %s ready at http://192.168.4.1", s_ap_ssid);
    return ESP_OK;
}

esp_err_t prov_softap_stop(void)
{
    s_stop_requested = true;
    if (s_listen_fd >= 0) {
        (void)close(s_listen_fd);
        s_listen_fd = -1;
    }
    /* 等待任务真正退出（最多 2 s），避免复用静态 TCB/栈创建新任务 */
    for (int i = 0; (i < 200) && s_task_active; i++) {
        vTaskDelay(pdMS_TO_TICKS(10U));
    }
    s_stop_requested = false;
    (void)esp_wifi_stop();
    return ESP_OK;
}

bool prov_softap_running(void)
{
    return s_running;
}


/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "net_weather.h"

#include <ctype.h>
#include <string.h>
#include <time.h>
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"

#define WEATHER_PERIOD_MS   (10 * 60 * 1000)
#define WEATHER_HTTP_BUF    512
#define WEATHER_CITY_MAX    48
#define WEATHER_NVS_NS      "weather"
#define WEATHER_NVS_CITY    "city"
#define WEATHER_NVS_UNIT    "unit"

static const char *TAG = "net_weather";

static SemaphoreHandle_t s_lock;
static weather_info_t s_weather;
static weather_forecast_t s_forecast[3];
static bool s_task_started = false;
static TaskHandle_t s_task_handle;
static char s_city[WEATHER_CITY_MAX] = "Changsha";
static bool s_unit_f = false;

typedef struct {
    char buf[WEATHER_HTTP_BUF];
    int len;
} weather_http_ctx_t;

void net_time_start(void)
{
    setenv("TZ", "CST-8", 1);
    tzset();

    if (esp_sntp_enabled()) {
        return;
    }
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "ntp.aliyun.com");
    esp_sntp_setservername(1, "pool.ntp.org");
    esp_sntp_init();
    ESP_LOGI(TAG, "SNTP started (TZ=CST-8)");
}

bool net_time_is_synced(void)
{
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    return (tm_now.tm_year + 1900) >= 2024;
}

static esp_err_t weather_http_event(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->user_data) {
        weather_http_ctx_t *ctx = (weather_http_ctx_t *)evt->user_data;
        int space = WEATHER_HTTP_BUF - 1 - ctx->len;
        if (space > 0) {
            int copy = evt->data_len < space ? evt->data_len : space;
            memcpy(ctx->buf + ctx->len, evt->data, copy);
            ctx->len += copy;
            ctx->buf[ctx->len] = '\0';
        }
    }
    return ESP_OK;
}

static void weather_trim(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ')) {
        s[--n] = '\0';
    }
}

/* Case-insensitive substring search. */
static bool weather_has(const char *hay, const char *needle)
{
    size_t nl = strlen(needle);
    for (const char *p = hay; *p; p++) {
        size_t i = 0;
        while (i < nl && p[i] && tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) {
            i++;
        }
        if (i == nl) {
            return true;
        }
    }
    return false;
}

/* Map wttr.in / WWO English condition text to a Chinese term whose glyphs are
 * all present in the bundled font subset. */
static const char *weather_cond_to_cn(const char *en)
{
    if (weather_has(en, "thunder")) {
        return weather_has(en, "snow") ? "雷阵雪" : "雷阵雨";
    }
    if (weather_has(en, "blizzard")) {
        return "暴雪";
    }
    if (weather_has(en, "sleet")) {
        return "雨夹雪";
    }
    if (weather_has(en, "snow")) {
        if (weather_has(en, "heavy")) return "大雪";
        if (weather_has(en, "shower")) return "阵雪";
        if (weather_has(en, "light") || weather_has(en, "patchy")) return "小雪";
        return "中雪";
    }
    if (weather_has(en, "ice") || weather_has(en, "pellet")) {
        return "冰雹";
    }
    if (weather_has(en, "freezing")) {
        return weather_has(en, "fog") ? "冻雾" : "冻雨";
    }
    if (weather_has(en, "drizzle")) {
        return "小雨";
    }
    if (weather_has(en, "rain") || weather_has(en, "shower")) {
        if (weather_has(en, "torrential") || weather_has(en, "heavy")) return "大雨";
        if (weather_has(en, "shower")) return "阵雨";
        if (weather_has(en, "light") || weather_has(en, "patchy")) return "小雨";
        return "中雨";
    }
    if (weather_has(en, "fog")) {
        return "雾";
    }
    if (weather_has(en, "mist") || weather_has(en, "haze")) {
        return "雾";
    }
    if (weather_has(en, "overcast")) {
        return "阴";
    }
    if (weather_has(en, "cloud")) {
        return "多云";
    }
    if (weather_has(en, "sunny") || weather_has(en, "clear")) {
        return "晴";
    }
    return "未知";
}

static void weather_build_url(char *out, size_t len)
{
    char city[WEATHER_CITY_MAX];
    bool unit_f;
    if (s_lock) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
    strlcpy(city, s_city, sizeof(city));
    unit_f = s_unit_f;
    if (s_lock) {
        xSemaphoreGive(s_lock);
    }
    /* wttr.in: %C condition, %t temp, %h humidity; u = Fahrenheit, m = metric. */
    snprintf(out, len, "https://wttr.in/%s?format=%%C|%%t|%%h&%s", city, unit_f ? "u" : "m");
}

static bool weather_fetch_once(void)
{
    char url[128];
    weather_build_url(url, sizeof(url));

    weather_http_ctx_t ctx = {0};
    esp_http_client_config_t config = {
        .url = url,
        .event_handler = weather_http_event,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 8000,
        .user_data = &ctx,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        return false;
    }
    esp_http_client_set_header(client, "Accept-Language", "en-US,en;q=0.9");
    /* Force English condition text; we map it to Chinese locally so the glyphs
     * always exist in the bundled font subset. */
    esp_http_client_set_header(client, "Accept-Language", "en-US,en;q=0.9");

    bool ok = false;
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    if (err == ESP_OK && status == 200 && ctx.len > 0) {
        weather_trim(ctx.buf);

        char cond[48] = {0};
        char temp[16] = {0};
        char humidity[16] = {0};

        char *p1 = strchr(ctx.buf, '|');
        char *p2 = p1 ? strchr(p1 + 1, '|') : NULL;
        if (p1 && p2) {
            *p1 = '\0';
            *p2 = '\0';
            const char *cn = weather_cond_to_cn(ctx.buf);
            strlcpy(cond, cn, sizeof(cond));
            strlcpy(temp, p1 + 1, sizeof(temp));
            strlcpy(humidity, p2 + 1, sizeof(humidity));

            time_t now = time(NULL);
            struct tm tm_now;
            localtime_r(&now, &tm_now);

            xSemaphoreTake(s_lock, portMAX_DELAY);
            strlcpy(s_weather.cond, cond, sizeof(s_weather.cond));
            strlcpy(s_weather.temp, temp, sizeof(s_weather.temp));
            strlcpy(s_weather.humidity, humidity, sizeof(s_weather.humidity));
            strftime(s_weather.updated, sizeof(s_weather.updated), "%H:%M", &tm_now);
            s_weather.valid = true;
            xSemaphoreGive(s_lock);

            ESP_LOGI(TAG, "Weather: %s %s %s", cond, temp, humidity);
            ok = true;
        } else {
            ESP_LOGW(TAG, "Unexpected weather payload: %s", ctx.buf);
        }
    } else {
        ESP_LOGW(TAG, "HTTP failed err=%s status=%d", esp_err_to_name(err), status);
    }

    esp_http_client_cleanup(client);
    return ok;
}

static bool weather_fetch_forecast_one(const char *url, weather_forecast_t *out, int day_off)
{
    weather_http_ctx_t ctx = {0};
    esp_http_client_config_t config = {
        .url = url, .event_handler = weather_http_event,
        .crt_bundle_attach = esp_crt_bundle_attach, .timeout_ms = 8000, .user_data = &ctx,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return false;
    esp_http_client_set_header(client, "Accept-Language", "en-US,en;q=0.9");
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    bool ok = false;
    if (err == ESP_OK && status == 200 && ctx.len > 0) {
        weather_trim(ctx.buf);
        time_t now = time(NULL);
        struct tm tm_now;
        time_t t = now + 86400 * day_off;
        localtime_r(&t, &tm_now);
        strftime(out->date, sizeof(out->date), "%m-%d", &tm_now);
        /* Parse "cond|temp" e.g. "Sunny|+22°C" */
        char *p = strchr(ctx.buf, '|');
        if (p) {
            *p = '\0'; p++;
            /* Strip " 1"/" 2" from condition text */
            size_t cl = strlen(ctx.buf);
            if (cl >= 2 && ctx.buf[cl-2] == ' ' && (ctx.buf[cl-1] == '1' || ctx.buf[cl-1] == '2'))
                ctx.buf[cl-2] = '\0';
            const char *cn = weather_cond_to_cn(ctx.buf);
            strlcpy(out->cond, cn, sizeof(out->cond));
            weather_trim(p);
            size_t tl = strlen(p);
            if (tl >= 2 && p[tl-2] == '+' && (p[tl-1] == '1' || p[tl-1] == '2'))
                p[tl-2] = '\0';
            else if (tl >= 2 && p[tl-2] == ' ' && (p[tl-1] == '1' || p[tl-1] == '2'))
                p[tl-2] = '\0';
            weather_trim(p);
            strlcpy(out->temp, p, sizeof(out->temp));
        } else {
            /* No pipe: just temperature */
            strlcpy(out->temp, ctx.buf, sizeof(out->temp));
        }
        out->valid = true;
        ok = true;
    }
    esp_http_client_cleanup(client);
    return ok;
}

static bool weather_fetch_forecast(void)
{
    char city[WEATHER_CITY_MAX]; bool unit_f;
    if (s_lock) { xSemaphoreTake(s_lock, portMAX_DELAY); }
    strlcpy(city, s_city, sizeof(city)); unit_f = s_unit_f;
    if (s_lock) { xSemaphoreGive(s_lock); }

    weather_forecast_t fc[2]; memset(fc, 0, sizeof(fc));
    char url[160];
    /* Fetch tomorrow (+1) and day after (+2) in two separate requests */
    snprintf(url, sizeof(url), "https://wttr.in/%s?format=%%C+1|%%t+1&%s", city, unit_f ? "u" : "m");
    bool ok1 = weather_fetch_forecast_one(url, &fc[0], 1);

    snprintf(url, sizeof(url), "https://wttr.in/%s?format=%%C+2|%%t+2&%s", city, unit_f ? "u" : "m");
    bool ok2 = weather_fetch_forecast_one(url, &fc[1], 2);

    if (ok1 || ok2) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        memcpy(&s_forecast[1], fc, sizeof(fc));
        xSemaphoreGive(s_lock);
        return true;
    }
    return false;
}

static void weather_build_forecast_url(char *out, size_t len)
{
    /* unused wrapper kept for compatibility, actual URL built inline */
    (void)out; (void)len;
}

static void weather_task(void *arg)
{
    (void)arg;
    while (1) {
        weather_fetch_once();
        weather_fetch_forecast();
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(WEATHER_PERIOD_MS));
    }
}

static void weather_load_nvs(void)
{
    nvs_handle_t handle;
    if (nvs_open(WEATHER_NVS_NS, NVS_READONLY, &handle) != ESP_OK) {
        return;
    }
    size_t len = sizeof(s_city);
    char city[WEATHER_CITY_MAX];
    if (nvs_get_str(handle, WEATHER_NVS_CITY, city, &len) == ESP_OK && city[0]) {
        strlcpy(s_city, city, sizeof(s_city));
    }
    uint8_t unit = 0;
    if (nvs_get_u8(handle, WEATHER_NVS_UNIT, &unit) == ESP_OK) {
        s_unit_f = unit ? true : false;
    }
    nvs_close(handle);
}

static void weather_save_nvs(void)
{
    nvs_handle_t handle;
    if (nvs_open(WEATHER_NVS_NS, NVS_READWRITE, &handle) != ESP_OK) {
        return;
    }
    nvs_set_str(handle, WEATHER_NVS_CITY, s_city);
    nvs_set_u8(handle, WEATHER_NVS_UNIT, s_unit_f ? 1 : 0);
    nvs_commit(handle);
    nvs_close(handle);
}

void net_weather_start(void)
{
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
    }
    if (s_task_started) {
        return;
    }
    weather_load_nvs();
    s_task_started = true;
    xTaskCreate(weather_task, "weather", 6144, NULL, 4, &s_task_handle);
}

void net_weather_set_city(const char *city)
{
    if (!city || !city[0]) {
        return;
    }
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    strlcpy(s_city, city, sizeof(s_city));
    s_weather.valid = false;
    memset(s_forecast, 0, sizeof(s_forecast));
    xSemaphoreGive(s_lock);
    weather_save_nvs();
    ESP_LOGI(TAG, "Weather city set to %s", city);
    if (s_task_handle) {
        xTaskNotifyGive(s_task_handle);
    }
}

const char *net_weather_get_city(void)
{
    return s_city;
}

void net_weather_set_fahrenheit(bool fahrenheit)
{
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_unit_f = fahrenheit;
    s_weather.valid = false;
    memset(s_forecast, 0, sizeof(s_forecast));
    xSemaphoreGive(s_lock);
    weather_save_nvs();
    if (s_task_handle) {
        xTaskNotifyGive(s_task_handle);
    }
}

bool net_weather_get_fahrenheit(void)
{
    return s_unit_f;
}

bool net_weather_get_forecast(weather_forecast_t out[3])
{
    if (!out || !s_lock) {
        return false;
    }
    /* Build today's entry from current weather + fill forecast[1..2]. */
    xSemaphoreTake(s_lock, portMAX_DELAY);
    memset(out, 0, sizeof(weather_forecast_t) * 3);

    /* Today: use headings from the HTML (current icons/temp) */
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    strftime(out[0].date, sizeof(out[0].date), "%m-%d", &tm_now);
    strlcpy(out[0].cond, s_weather.cond, sizeof(out[0].cond));
    strlcpy(out[0].temp, s_weather.temp, sizeof(out[0].temp));
    out[0].valid = s_weather.valid;

    /* Tomorrow and day after */
    memcpy(&out[1], &s_forecast[1], sizeof(weather_forecast_t));
    memcpy(&out[2], &s_forecast[2], sizeof(weather_forecast_t));

    xSemaphoreGive(s_lock);
    return out[0].valid;
}

bool net_weather_get(weather_info_t *out)
{
    if (!out || !s_lock) {
        return false;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_weather;
    xSemaphoreGive(s_lock);
    return out->valid;
}

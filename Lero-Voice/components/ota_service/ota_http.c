/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file ota_http.c
 * @brief HTTP channel: meta.json fetch + streaming download into the OTA slot.
 *
 * The download writes the inactive slot directly (docs/PLAN.md 8.2);
 * esp_ota_end() validates the image structure afterwards.
 */

#include <string.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "ota_internal.h"

#define TAG "ota_http"

#define META_BUF_SIZE   4096U
#define APP_URL_MAX     256U

/* ota_service is single-instance; a static meta buffer is acceptable. */
static uint8_t s_meta_buf[META_BUF_SIZE];

static esp_err_t s_build_app_url(const char *meta_url, const char *app_bin,
                                 char *out, size_t out_size)
{
    if ((meta_url == NULL) || (app_bin == NULL) || (out == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    const char *slash = strrchr(meta_url, '/');
    if (slash == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const size_t base_len = (size_t)(slash - meta_url) + 1U;
    if ((base_len + strlen(app_bin) + 1U) > out_size) {
        return ESP_ERR_INVALID_SIZE;
    }
    (void)memcpy(out, meta_url, base_len);
    (void)strcpy(out + base_len, app_bin);
    return ESP_OK;
}

esp_err_t ota_http_fetch_meta(const char *url, ota_meta_t *meta)
{
    if ((url == NULL) || (meta == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 15000,
        .buffer_size = META_BUF_SIZE,
        .keep_alive_enable = false,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        ESP_LOGE(TAG, "http client init failed");
        return ESP_FAIL;
    }
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "open %s failed: %s", url, esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }
    (void)esp_http_client_fetch_headers(client);
    size_t total = 0U;
    while (total < (sizeof(s_meta_buf) - 1U)) {
        const int n = esp_http_client_read(client, (char *)&s_meta_buf[total],
                                           (int)(sizeof(s_meta_buf) - 1U - total));
        if (n <= 0) {
            break;
        }
        total += (size_t)n;
    }
    s_meta_buf[total] = '\0';
    (void)esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (total == 0U) {
        ESP_LOGE(TAG, "empty meta response");
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ota_meta_parse((const char *)s_meta_buf, total, meta);
}

static esp_err_t s_http_read(void *ctx, uint8_t *buf, size_t buf_size, size_t *read_len)
{
    esp_http_client_handle_t client = (esp_http_client_handle_t)ctx;
    const int n = esp_http_client_read(client, (char *)buf, (int)buf_size);
    if (n < 0) {
        ESP_LOGE(TAG, "http read failed");
        return ESP_FAIL;
    }
    *read_len = (size_t)n;
    return ESP_OK;
}

esp_err_t ota_http_download(const char *url, const esp_partition_t *part,
                            const ota_meta_t *meta, ota_progress_fn_t progress,
                            void *ctx)
{
    if ((url == NULL) || (part == NULL) || (meta == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    char app_url[APP_URL_MAX];
    esp_err_t err = s_build_app_url(url, meta->app_bin, app_url, sizeof(app_url));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "build app url failed");
        return err;
    }

    esp_http_client_config_t cfg = {
        .url = app_url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = CONFIG_LERO_OTA_HTTP_TIMEOUT_MS,
        .buffer_size = CONFIG_LERO_OTA_CHUNK_SIZE,
        .keep_alive_enable = false,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        ESP_LOGE(TAG, "http client init failed");
        return ESP_FAIL;
    }
    err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "open %s failed: %s", app_url, esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }
    const int64_t content_len = esp_http_client_fetch_headers(client);
    if ((content_len >= 0) && ((uint64_t)content_len != meta->app_bin_size)) {
        ESP_LOGE(TAG, "content length mismatch: %lld expected %u",
                 (long long)content_len, (unsigned)meta->app_bin_size);
        (void)esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_INVALID_SIZE;
    }
    ESP_LOGI(TAG, "downloading %s (%u bytes)", app_url, (unsigned)meta->app_bin_size);

    uint8_t sha256[32];
    err = ota_partition_write(part, s_http_read, client, meta->app_bin_size,
                              progress, ctx, sha256);
    (void)esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "download/write failed: %s", esp_err_to_name(err));
        return err;
    }
    return ota_verify_sha256_hex(sha256, meta->app_sha256);
}


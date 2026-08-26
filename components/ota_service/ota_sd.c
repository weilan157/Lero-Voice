/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file ota_sd.c
 * @brief SD channel: /update/ scanning + force upgrade (downgrade allowed).
 *
 * Directory layout (docs/PLAN.md 8.5):
 *   /update/lero_app.bin
 *   /update/lero_app.bin.sha256   (hex string, one line)
 *   /update/update.json           (optional meta)
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "esp_log.h"
#include "bsp_sdcard.h"
#include "ota_internal.h"

#define TAG "ota_sd"

#define SD_BUF_SIZE    4096U
#define SD_PATH_MAX    160U

static esp_err_t s_read_sha256_file(const char *path, char *out, size_t out_size)
{
    out[0] = '\0';
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    char line[80];
    if (fgets(line, sizeof(line), f) != NULL) {
        size_t len = strlen(line);
        while ((len > 0U) && (isspace((unsigned char)line[len - 1U]))) {
            line[--len] = '\0';
        }
        (void)strlcpy(out, line, out_size);
    }
    (void)fclose(f);
    return (out[0] != '\0') ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

static esp_err_t s_file_size(FILE *f, uint32_t *size)
{
    if ((fseek(f, 0L, SEEK_END) != 0) || (ftell(f) < 0)) {
        return ESP_FAIL;
    }
    const long len = ftell(f);
    if ((len <= 0L) || ((unsigned long)len > 0xFFFFFFFFUL)) {
        return ESP_ERR_INVALID_SIZE;
    }
    (void)fseek(f, 0L, SEEK_SET);
    *size = (uint32_t)len;
    return ESP_OK;
}

esp_err_t ota_sd_find_update(ota_meta_t *meta)
{
    if (meta == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = bsp_sdcard_poll();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SD card unavailable: %s", esp_err_to_name(err));
        return err;
    }
    (void)memset(meta, 0, sizeof(*meta));
    (void)strlcpy(meta->app_bin, "lero_app.bin", sizeof(meta->app_bin));

    /* 可选 update.json */
    char path[SD_PATH_MAX];
    (void)snprintf(path, sizeof(path), "%s/update.json", CONFIG_LERO_OTA_SD_UPDATE_DIR);
    FILE *f = fopen(path, "r");
    if (f != NULL) {
        static char s_json[SD_BUF_SIZE];
        const size_t n = fread(s_json, 1U, sizeof(s_json) - 1U, f);
        s_json[n] = '\0';
        (void)fclose(f);
        (void)ota_meta_parse(s_json, n, meta);
    }

    /* 固件文件必须存在 */
    (void)snprintf(path, sizeof(path), "%s/%s", CONFIG_LERO_OTA_SD_UPDATE_DIR, meta->app_bin);
    f = fopen(path, "rb");
    if (f == NULL) {
        ESP_LOGE(TAG, "no %s on SD", path);
        return ESP_ERR_NOT_FOUND;
    }
    if (s_file_size(f, &meta->app_bin_size) != ESP_OK) {
        (void)fclose(f);
        return ESP_ERR_INVALID_SIZE;
    }
    (void)fclose(f);

    /* 可选 sha256 参考文件 */
    (void)snprintf(path, sizeof(path), "%s/%s.sha256", CONFIG_LERO_OTA_SD_UPDATE_DIR, meta->app_bin);
    (void)s_read_sha256_file(path, meta->app_sha256, sizeof(meta->app_sha256));

    if (meta->version[0] == '\0') {
        (void)strlcpy(meta->version, "sd", sizeof(meta->version));
    }
    ESP_LOGI(TAG, "SD update found: %s (%u bytes, sha256 %s)",
             meta->app_bin, (unsigned)meta->app_bin_size,
             meta->app_sha256[0] != '\0' ? "present" : "absent");
    return ESP_OK;
}

static esp_err_t s_file_read(void *ctx, uint8_t *buf, size_t buf_size, size_t *read_len)
{
    FILE *f = (FILE *)ctx;
    const size_t n = fread(buf, 1U, buf_size, f);
    *read_len = n;
    if ((n == 0U) && (ferror(f) != 0)) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t ota_sd_write_partition(const esp_partition_t *part, const ota_meta_t *meta,
                                 ota_progress_fn_t progress, void *ctx)
{
    if ((part == NULL) || (meta == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    char path[SD_PATH_MAX];
    (void)snprintf(path, sizeof(path), "%s/%s", CONFIG_LERO_OTA_SD_UPDATE_DIR, meta->app_bin);
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        ESP_LOGE(TAG, "open %s failed", path);
        return ESP_ERR_NOT_FOUND;
    }
    uint8_t sha256[32];
    esp_err_t err = ota_partition_write(part, s_file_read, f, meta->app_bin_size,
                                        progress, ctx, sha256);
    (void)fclose(f);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SD write failed: %s", esp_err_to_name(err));
        return err;
    }
    if (ota_hex_str_valid(meta->app_sha256, 64U)) {
        return ota_verify_sha256_hex(sha256, meta->app_sha256);
    }
    ESP_LOGW(TAG, "no reference sha256; structure check only (esp_ota_end)");
    return ESP_OK;
}


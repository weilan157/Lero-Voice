/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file ota_meta.c
 * @brief meta.json fetch-independent parsing / validation (docs/PLAN.md 8.7).
 *
 * Minimal fixed-buffer JSON scanner (no cJSON -> no dynamic memory). Field
 * values are simple strings / numbers / booleans without nested structures.
 */

#include <string.h>
#include <strings.h>
#include <ctype.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "partition_table_sha256.h"
#include "esp_app_desc.h"
#include "ota_internal.h"

#define TAG "ota_meta"

static const char *s_current_version(void)
{
    const esp_app_desc_t *desc = esp_app_get_description();
    return (desc != NULL) ? desc->version : "?";
}

static const char *s_find_value(const char *json, size_t len, const char *key)
{
    const size_t key_len = strlen(key);
    for (size_t i = 0U; (i + key_len + 2U) < len; i++) {
        if ((json[i] == '"') && (strncmp(&json[i + 1U], key, key_len) == 0) &&
            (json[i + 1U + key_len] == '"')) {
            size_t j = i + key_len + 2U;                 /* 跳过 "key" */
            while ((j < len) && (json[j] == ' ' || json[j] == '\t' || json[j] == '\n')) {
                j++;
            }
            if ((j < len) && (json[j] == ':')) {
                j++;
                while ((j < len) && (json[j] == ' ' || json[j] == '\t' || json[j] == '\n')) {
                    j++;
                }
                return &json[j];
            }
        }
    }
    return NULL;
}

static esp_err_t s_parse_str(const char *json, size_t len, const char *key,
                             char *out, size_t out_size)
{
    if ((out == NULL) || (out_size == 0U)) {
        return ESP_ERR_INVALID_ARG;
    }
    out[0] = '\0';
    const char *v = s_find_value(json, len, key);
    if ((v == NULL) || (*v != '"')) {
        return ESP_ERR_NOT_FOUND;
    }
    size_t di = 0U;
    for (size_t i = 1U; (v[i] != '\0') && (v[i] != '"') && (di + 1U < out_size); i++) {
        out[di++] = v[i];
    }
    out[di] = '\0';
    return ESP_OK;
}

static esp_err_t s_parse_u32(const char *json, size_t len, const char *key, uint32_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const char *v = s_find_value(json, len, key);
    if (v == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    if (!isdigit((unsigned char)*v)) {
        return ESP_ERR_NOT_FOUND;
    }
    uint32_t val = 0U;
    size_t i = 0U;
    while (isdigit((unsigned char)v[i])) {
        val = (val * 10U) + (uint32_t)(v[i] - '0');
        i++;
    }
    *out = val;
    return ESP_OK;
}

static esp_err_t s_parse_bool(const char *json, size_t len, const char *key, bool *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const char *v = s_find_value(json, len, key);
    if (v == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    if (strncmp(v, "true", 4U) == 0) {
        *out = true;
        return ESP_OK;
    }
    if (strncmp(v, "false", 5U) == 0) {
        *out = false;
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t ota_meta_parse(const char *json, size_t json_len, ota_meta_t *meta)
{
    if ((json == NULL) || (meta == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    (void)memset(meta, 0, sizeof(*meta));

    (void)s_parse_str(json, json_len, "version", meta->version, sizeof(meta->version));
    (void)s_parse_str(json, json_len, "min_app_version", meta->min_app_version,
                      sizeof(meta->min_app_version));
    (void)s_parse_str(json, json_len, "target", meta->target, sizeof(meta->target));
    (void)s_parse_str(json, json_len, "soc", meta->soc, sizeof(meta->soc));
    (void)s_parse_u32(json, json_len, "flash_size", &meta->flash_size);
    (void)s_parse_bool(json, json_len, "psram", &meta->psram);
    (void)s_parse_str(json, json_len, "app_bin", meta->app_bin, sizeof(meta->app_bin));
    (void)s_parse_u32(json, json_len, "app_bin_size", &meta->app_bin_size);
    (void)s_parse_str(json, json_len, "app_sha256", meta->app_sha256,
                      sizeof(meta->app_sha256));
    (void)s_parse_str(json, json_len, "partition_table_sha256", meta->partition_table_sha256,
                      sizeof(meta->partition_table_sha256));
    (void)s_parse_str(json, json_len, "idf_version", meta->idf_version,
                      sizeof(meta->idf_version));
    (void)s_parse_str(json, json_len, "build_date", meta->build_date,
                      sizeof(meta->build_date));
    (void)s_parse_str(json, json_len, "release_notes", meta->release_notes,
                      sizeof(meta->release_notes));
    return ESP_OK;
}

bool ota_hex_str_valid(const char *hex, size_t len)
{
    if ((hex == NULL) || (len != 64U)) {
        return false;
    }
    for (size_t i = 0U; i < len; i++) {
        const char c = hex[i];
        if (!(((c >= '0') && (c <= '9')) || ((c >= 'a') && (c <= 'f')) ||
              ((c >= 'A') && (c <= 'F')))) {
            return false;
        }
    }
    return true;
}

typedef struct {
    int major;
    int minor;
    int patch;
    bool prerelease;
} s_version_t;

static int s_atoi_strict(const char **p)
{
    int val = 0;
    while (**p && (**p >= '0') && (**p <= '9')) {
        val = (val * 10) + (**p - '0');
        (*p)++;
    }
    return val;
}

static esp_err_t s_parse_version(const char *s, s_version_t *v)
{
    if ((s == NULL) || (v == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    const char *p = s;
    if ((*p == 'v') || (*p == 'V')) {
        p++;
    }
    v->major = s_atoi_strict(&p);
    if (*p != '.') {
        return ESP_ERR_INVALID_ARG;
    }
    p++;
    v->minor = s_atoi_strict(&p);
    if (*p != '.') {
        return ESP_ERR_INVALID_ARG;
    }
    p++;
    v->patch = s_atoi_strict(&p);
    v->prerelease = (*p == '-');
    return ESP_OK;
}

int ota_version_compare(const char *a, const char *b)
{
    s_version_t va;
    s_version_t vb;
    if ((s_parse_version(a, &va) != ESP_OK) || (s_parse_version(b, &vb) != ESP_OK)) {
        return 0;
    }
    if (va.major != vb.major) {
        return (va.major > vb.major) ? 1 : -1;
    }
    if (va.minor != vb.minor) {
        return (va.minor > vb.minor) ? 1 : -1;
    }
    if (va.patch != vb.patch) {
        return (va.patch > vb.patch) ? 1 : -1;
    }
    if (va.prerelease != vb.prerelease) {
        return va.prerelease ? -1 : 1;   /* 预发布低于正式版（8.11 #12） */
    }
    return 0;
}

esp_err_t ota_meta_validate(const ota_meta_t *meta, bool *is_newer)
{
    if ((meta == NULL) || (is_newer == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    *is_newer = false;

    if (strcmp(meta->target, CONFIG_LERO_OTA_TARGET) != 0) {
        ESP_LOGE(TAG, "target mismatch: meta=%s expected=%s",
                 meta->target, CONFIG_LERO_OTA_TARGET);
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (strcmp(meta->soc, CONFIG_LERO_OTA_SOC) != 0) {
        ESP_LOGE(TAG, "soc mismatch: meta=%s expected=%s",
                 meta->soc, CONFIG_LERO_OTA_SOC);
        return ESP_ERR_NOT_SUPPORTED;
    }
    if ((meta->flash_size != 0U) && (meta->flash_size != (uint32_t)CONFIG_LERO_OTA_FLASH_SIZE)) {
        ESP_LOGE(TAG, "flash size mismatch: meta=%u expected=%u",
                 (unsigned)meta->flash_size, (unsigned)CONFIG_LERO_OTA_FLASH_SIZE);
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (meta->app_bin_size == 0U) {
        ESP_LOGE(TAG, "meta app_bin_size missing");
        return ESP_ERR_INVALID_ARG;
    }
    if (!ota_hex_str_valid(meta->app_sha256, 64U)) {
        ESP_LOGE(TAG, "meta app_sha256 missing/invalid");
        return ESP_ERR_INVALID_ARG;
    }
    if (meta->partition_table_sha256[0] != '\0') {
        if (strcasecmp(meta->partition_table_sha256, LERO_PARTITION_TABLE_SHA256) != 0) {
            ESP_LOGE(TAG, "partition table mismatch -> use SD/full flash (8.11 #7)");
            return ESP_ERR_NOT_SUPPORTED;
        }
    }
    if (meta->min_app_version[0] != '\0') {
        if (ota_version_compare(s_current_version(), meta->min_app_version) < 0) {
            ESP_LOGE(TAG, "current %s below min_app_version %s",
                     s_current_version(), meta->min_app_version);
            return ESP_ERR_NOT_SUPPORTED;
        }
    }
    if (ota_version_compare(meta->version, s_current_version()) > 0) {
        *is_newer = true;
    }
    return ESP_OK;
}


/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file ota_verify.c
 * @brief SHA-256 verification helpers (docs/PLAN.md 8.3 / 8.9).
 *
 * The image structure itself is validated by esp_ota_end(); this module only
 * compares SHA-256 digests against the reference from meta.json / .sha256.
 */

#include <string.h>
#include <strings.h>
#include "esp_log.h"
#include "esp_partition.h"
#include "mbedtls/md.h"
#include "ota_internal.h"

#define TAG "ota_verify"

#define SHA256_DIGEST_LEN  32U
#define VERIFY_CHUNK       4096U

static void s_hex_encode(const uint8_t *data, size_t len, char *out)
{
    static const char s_hex[] = "0123456789abcdef";
    for (size_t i = 0U; i < len; i++) {
        out[i * 2U] = s_hex[data[i] >> 4U];
        out[(i * 2U) + 1U] = s_hex[data[i] & 0x0FU];
    }
    out[len * 2U] = '\0';
}

esp_err_t ota_verify_sha256_hex(const uint8_t *digest, const char *expected_hex)
{
    if ((digest == NULL) || !ota_hex_str_valid(expected_hex, 64U)) {
        return ESP_ERR_INVALID_ARG;
    }
    char hex[65];
    s_hex_encode(digest, SHA256_DIGEST_LEN, hex);
    if (strcasecmp(hex, expected_hex) != 0) {
        ESP_LOGE(TAG, "sha256 mismatch: got %s expected %s", hex, expected_hex);
        return ESP_ERR_INVALID_CRC;
    }
    return ESP_OK;
}

static esp_err_t s_sha256_compute(const uint8_t *buf, size_t len, uint8_t digest[SHA256_DIGEST_LEN])
{
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (info == NULL) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    int rc = mbedtls_md_setup(&ctx, info, 0);
    if (rc == 0) {
        rc = mbedtls_md_starts(&ctx);
    }
    if (rc == 0) {
        rc = mbedtls_md_update(&ctx, buf, len);
    }
    if (rc == 0) {
        rc = mbedtls_md_finish(&ctx, digest);
    }
    mbedtls_md_free(&ctx);
    return (rc == 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t ota_verify_file_sha256(FILE *f, size_t total_len, const char *expected_hex,
                                 ota_progress_fn_t progress, void *ctx)
{
    if (f == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (info == NULL) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    mbedtls_md_context_t md;
    mbedtls_md_init(&md);
    int rc = mbedtls_md_setup(&md, info, 0);
    if (rc == 0) {
        rc = mbedtls_md_starts(&md);
    }
    if (rc != 0) {
        mbedtls_md_free(&md);
        return ESP_FAIL;
    }
    (void)fseek(f, 0L, SEEK_SET);
    uint8_t buf[VERIFY_CHUNK];
    size_t total = 0U;
    esp_err_t err = ESP_OK;
    while (total < total_len) {
        const size_t want = (total_len - total < sizeof(buf)) ? (total_len - total) : sizeof(buf);
        const size_t got = fread(buf, 1U, want, f);
        if (got == 0U) {
            err = ESP_ERR_INVALID_SIZE;
            break;
        }
        rc = mbedtls_md_update(&md, buf, got);
        if (rc != 0) {
            err = ESP_FAIL;
            break;
        }
        total += got;
        if (progress != NULL) {
            progress((uint8_t)((total * 100U) / total_len), ctx);
        }
    }
    uint8_t digest[SHA256_DIGEST_LEN];
    if ((err == ESP_OK) && (total == total_len)) {
        rc = mbedtls_md_finish(&md, digest);
        err = (rc == 0) ? ESP_OK : ESP_FAIL;
    }
    mbedtls_md_free(&md);
    if (err != ESP_OK) {
        return err;
    }
    return ota_verify_sha256_hex(digest, expected_hex);
}

esp_err_t ota_verify_flash_sha256(const esp_partition_t *part, const char *expected_hex,
                                  ota_progress_fn_t progress, void *ctx)
{
    if (part == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const size_t size = part->size;
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (info == NULL) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    mbedtls_md_context_t md;
    mbedtls_md_init(&md);
    int rc = mbedtls_md_setup(&md, info, 0);
    if (rc == 0) {
        rc = mbedtls_md_starts(&md);
    }
    if (rc != 0) {
        mbedtls_md_free(&md);
        return ESP_FAIL;
    }
    uint8_t buf[VERIFY_CHUNK];
    size_t offset = 0U;
    esp_err_t err = ESP_OK;
    while (offset < size) {
        const size_t want = ((size - offset) < sizeof(buf)) ? (size - offset) : sizeof(buf);
        err = esp_partition_read(part, offset, buf, want);
        if (err != ESP_OK) {
            break;
        }
        rc = mbedtls_md_update(&md, buf, want);
        if (rc != 0) {
            err = ESP_FAIL;
            break;
        }
        offset += want;
        if (progress != NULL) {
            progress((uint8_t)((offset * 100U) / size), ctx);
        }
    }
    uint8_t digest[SHA256_DIGEST_LEN];
    if ((err == ESP_OK) && (offset == size)) {
        rc = mbedtls_md_finish(&md, digest);
        err = (rc == 0) ? ESP_OK : ESP_FAIL;
    }
    mbedtls_md_free(&md);
    if (err != ESP_OK) {
        return err;
    }
    return ota_verify_sha256_hex(digest, expected_hex);
}

esp_err_t ota_verify_buffer_sha256(const uint8_t *buf, size_t len, const char *expected_hex)
{
    uint8_t digest[SHA256_DIGEST_LEN];
    esp_err_t err = s_sha256_compute(buf, len, digest);
    if (err != ESP_OK) {
        return err;
    }
    return ota_verify_sha256_hex(digest, expected_hex);
}


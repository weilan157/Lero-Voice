/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file ota_internal.h
 * @brief Internal interface shared by the ota_service modules.
 */

#ifndef OTA_INTERNAL_H
#define OTA_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include "esp_err.h"
#include "esp_partition.h"
#include "ota_service.h"

#define OTA_META_VERSION_MAX    16U
#define OTA_META_NOTES_MAX      128U
#define OTA_META_TARGET_MAX     16U
#define OTA_META_BIN_MAX        32U
#define OTA_SHA256_HEX_LEN      65U

typedef struct {
    char version[OTA_META_VERSION_MAX];
    char min_app_version[OTA_META_VERSION_MAX];
    char target[OTA_META_TARGET_MAX];
    char soc[OTA_META_TARGET_MAX];
    uint32_t flash_size;
    bool psram;
    char app_bin[OTA_META_BIN_MAX];
    uint32_t app_bin_size;
    char app_sha256[OTA_SHA256_HEX_LEN];
    char partition_table_sha256[OTA_SHA256_HEX_LEN];
    char idf_version[16];
    char build_date[32];
    char release_notes[OTA_META_NOTES_MAX];
} ota_meta_t;

typedef void (*ota_progress_fn_t)(uint8_t pct, void *ctx);

/* ota_meta.c */
esp_err_t ota_meta_parse(const char *json, size_t json_len, ota_meta_t *meta);
esp_err_t ota_meta_validate(const ota_meta_t *meta, bool *is_newer);
int ota_version_compare(const char *a, const char *b);
bool ota_hex_str_valid(const char *hex, size_t len);

/* ota_verify.c */
esp_err_t ota_verify_sha256_hex(const uint8_t *digest, const char *expected_hex);
esp_err_t ota_verify_buffer_sha256(const uint8_t *buf, size_t len, const char *expected_hex);
esp_err_t ota_verify_file_sha256(FILE *f, size_t total_len, const char *expected_hex,
                                 ota_progress_fn_t progress, void *ctx);
esp_err_t ota_verify_flash_sha256(const esp_partition_t *part, const char *expected_hex,
                                  ota_progress_fn_t progress, void *ctx);

/* ota_partition.c */
esp_err_t ota_partition_get_next(const esp_partition_t **part);
esp_err_t ota_partition_get_running_desc(char *version, size_t vlen,
                                         char *label, size_t llen);
esp_err_t ota_partition_get_next_desc(char *label, size_t llen);
esp_err_t ota_partition_set_boot_to_next(void);
esp_err_t ota_partition_mark_valid(void);

typedef esp_err_t (*ota_read_fn_t)(void *ctx, uint8_t *buf, size_t buf_size,
                                   size_t *read_len);
esp_err_t ota_partition_write(const esp_partition_t *part, ota_read_fn_t read_fn,
                              void *ctx, size_t expected_size,
                              ota_progress_fn_t progress, void *progress_ctx,
                              uint8_t sha256_out[32]);

/* ota_state.c */
esp_err_t ota_state_init(void);
esp_err_t ota_state_set_pending(const ota_meta_t *meta, ota_channel_t channel);
esp_err_t ota_state_clear_pending(void);
esp_err_t ota_state_get_pending(bool *pending, char *version, size_t vlen,
                                ota_channel_t *channel);
esp_err_t ota_state_set_result(ota_result_t result, const char *detail);
esp_err_t ota_state_get_result(ota_result_t *result, char *detail, size_t dlen);
esp_err_t ota_state_battery_gate(bool *ok);
esp_err_t ota_state_incr_attempts(void);
esp_err_t ota_state_reset_attempts(void);

/* ota_http.c */
esp_err_t ota_http_fetch_meta(const char *url, ota_meta_t *meta);
esp_err_t ota_http_download(const char *url, const esp_partition_t *part,
                            const ota_meta_t *meta, ota_progress_fn_t progress,
                            void *ctx);

/* ota_sd.c */
esp_err_t ota_sd_find_update(ota_meta_t *meta);
esp_err_t ota_sd_write_partition(const esp_partition_t *part, const ota_meta_t *meta,
                                 ota_progress_fn_t progress, void *ctx);

/* ota_service.c */
void ota_notify(ota_state_t state, const char *version, uint8_t pct);
void ota_set_state(ota_state_t state, const char *version, uint8_t pct);
const char *ota_state_name(ota_state_t state);

#endif /* OTA_INTERNAL_H */


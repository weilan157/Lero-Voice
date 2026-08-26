/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file ota_partition.c
 * @brief OTA slot selection / streaming write / switch / rollback confirm.
 *
 * Writes go to the INACTIVE slot only (docs/PLAN.md 8.2 / 8.3). SHA-256 is
 * computed while writing so the flash is read only once.
 */

#include <string.h>
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "mbedtls/md.h"
#include "ota_internal.h"

#define TAG "ota_partition"

#define SHA256_DIGEST_LEN  32U

esp_err_t ota_partition_get_next(const esp_partition_t **part)
{
    if (part == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *part = esp_ota_get_next_update_partition(NULL);
    if (*part == NULL) {
        ESP_LOGE(TAG, "no OTA slot available (check partition table)");
        return ESP_ERR_NOT_FOUND;
    }
    return ESP_OK;
}

esp_err_t ota_partition_get_running_desc(char *version, size_t vlen,
                                         char *label, size_t llen)
{
    if (version == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    if (label != NULL) {
        (void)strlcpy(label, running->label, llen);
    }
    esp_app_desc_t desc;
    (void)memset(&desc, 0, sizeof(desc));
    esp_err_t err = esp_ota_get_partition_description(running, &desc);
    if (err == ESP_OK) {
        (void)strlcpy(version, desc.version, vlen);
    } else {
        (void)strlcpy(version, "?", vlen);
    }
    return ESP_OK;
}

esp_err_t ota_partition_get_next_desc(char *label, size_t llen)
{
    if (label == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    if (next == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    (void)strlcpy(label, next->label, llen);
    return ESP_OK;
}

esp_err_t ota_partition_set_boot_to_next(void)
{
    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    if (next == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    esp_err_t err = esp_ota_set_boot_partition(next);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "boot partition set to %s", next->label);
    } else {
        ESP_LOGE(TAG, "set boot partition failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t ota_partition_mark_valid(void)
{
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "app marked valid (rollback cancelled)");
    }
    return err;
}

/* 升级后启动时的回滚确认判定（PLAN 8.4 step 7 / 8.8）：
 * 仅当运行分区处于 PENDING_VERIFY 时返回 pending_verify=true；
 * healthy = 版本自证通过（运行版本 == NVS 记录的 pending 版本）。
 * 自证失败时不标记有效，bootloader 将自动回滚旧槽。 */
esp_err_t ota_partition_boot_state(bool *pending_verify, bool *healthy)
{
    if ((pending_verify == NULL) || (healthy == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    *pending_verify = false;
    *healthy = false;

    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    esp_err_t err = esp_ota_get_state_partition(running, &state);
    if (err != ESP_OK) {
        return err;
    }
    if (state != ESP_OTA_IMG_PENDING_VERIFY) {
        *healthy = true;                /* 非待验证状态无需处理 */
        return ESP_OK;
    }
    *pending_verify = true;

    bool pending = false;
    char pending_version[OTA_VERSION_MAX];
    ota_channel_t channel = OTA_CHANNEL_HTTP;
    char running_version[OTA_VERSION_MAX];
    if ((ota_state_get_pending(&pending, pending_version, sizeof(pending_version),
                               &channel) == ESP_OK) && pending &&
        (ota_partition_get_running_desc(running_version, sizeof(running_version),
                                        NULL, 0U) == ESP_OK)) {
        *healthy = (strcmp(running_version, pending_version) == 0);
        if (!*healthy) {
            ESP_LOGE(TAG, "version self-check failed: running=%s pending=%s",
                     running_version, pending_version);
        }
    }
    return ESP_OK;
}

static esp_err_t s_md_finish_to_hex(mbedtls_md_context_t *md, uint8_t out[32])
{
    uint8_t digest[SHA256_DIGEST_LEN];
    int rc = mbedtls_md_finish(md, digest);
    if (rc != 0) {
        return ESP_FAIL;
    }
    (void)memcpy(out, digest, SHA256_DIGEST_LEN);
    return ESP_OK;
}

esp_err_t ota_partition_write(const esp_partition_t *part, ota_read_fn_t read_fn,
                              void *ctx, size_t expected_size,
                              ota_progress_fn_t progress, void *progress_ctx,
                              uint8_t sha256_out[32])
{
    if ((part == NULL) || (read_fn == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_ota_handle_t handle = 0U;
    esp_err_t err = esp_ota_begin(part, expected_size, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        return err;
    }

    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (info == NULL) {
        (void)esp_ota_abort(handle);
        return ESP_ERR_NOT_SUPPORTED;
    }
    mbedtls_md_context_t md;
    mbedtls_md_init(&md);
    if (mbedtls_md_setup(&md, info, 0) != 0) {
        (void)esp_ota_abort(handle);
        return ESP_FAIL;
    }
    (void)mbedtls_md_starts(&md);

    static uint8_t s_buf[4096];
    size_t total = 0U;
    for (;;) {
        size_t n = 0U;
        err = read_fn(ctx, s_buf, sizeof(s_buf), &n);
        if (err != ESP_OK) {
            break;
        }
        if (n == 0U) {
            break;                      /* EOF */
        }
        if ((total + n) > expected_size) {
            err = ESP_ERR_INVALID_SIZE;
            break;
        }
        err = esp_ota_write(handle, s_buf, n);
        if (err != ESP_OK) {
            break;
        }
        (void)mbedtls_md_update(&md, s_buf, n);
        total += n;
        if (progress != NULL) {
            progress((uint8_t)((total * 100U) / expected_size), progress_ctx);
        }
    }

    if ((err == ESP_OK) && (total != expected_size)) {
        err = ESP_ERR_INVALID_SIZE;
    }
    if (err == ESP_OK) {
        err = esp_ota_end(handle);      /* 镜像结构校验（8.2 直写 OTA 槽） */
        if (err == ESP_OK) {
            err = s_md_finish_to_hex(&md, sha256_out);
        }
    } else {
        (void)esp_ota_abort(handle);
    }
    mbedtls_md_free(&md);
    return err;
}


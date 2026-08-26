/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file bsp_storage.c
 * @brief SPIFFS "storage" partition management.
 */

#include <string.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "nvs.h"
#include "bsp_storage.h"

#define TAG "bsp_storage"

#define STORAGE_PARTITION_LABEL   "storage"
#define STORAGE_MAX_FILES         8U
#define NVS_NS_SYS                "sys"
#define NVS_KEY_BOOT_CNT          "boot_cnt"

static bool s_mounted;

/* 启动计数：返回 true 表示首次启动（NVS 中无 boot_cnt）。
 * 仅用于"首次上电自动格式化 storage"，之后挂载失败只告警（PLAN 8.6 #3）。 */
static esp_err_t s_boot_count_inc(bool *first_boot)
{
    *first_boot = false;
    nvs_handle_t h = 0U;
    esp_err_t err = nvs_open(NVS_NS_SYS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    uint32_t cnt = 0U;
    err = nvs_get_u32(h, NVS_KEY_BOOT_CNT, &cnt);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *first_boot = true;
        err = nvs_set_u32(h, NVS_KEY_BOOT_CNT, 1U);
    } else if (err == ESP_OK) {
        err = nvs_set_u32(h, NVS_KEY_BOOT_CNT, cnt + 1U);
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    (void)nvs_close(h);
    return err;
}

esp_err_t bsp_storage_init(void)
{
    return bsp_storage_mount();
}

esp_err_t bsp_storage_mount(void)
{
    if (s_mounted) {
        return ESP_OK;
    }
    bool first_boot = false;
    (void)s_boot_count_inc(&first_boot);

    esp_vfs_spiffs_conf_t conf = {
        .base_path = CONFIG_LERO_STORAGE_BASE_PATH,
        .partition_label = STORAGE_PARTITION_LABEL,
        .max_files = STORAGE_MAX_FILES,
        .format_if_mount_failed = false,   /* PLAN 8.6 #3: 挂载失败仅告警 */
    };
    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mount failed: %s", esp_err_to_name(err));
        /* 首次上电自动格式化一次（工厂出厂态），之后仅告警 */
        if (first_boot) {
            ESP_LOGW(TAG, "first boot: formatting storage once");
            if (bsp_storage_format() == ESP_OK) {
                err = esp_vfs_spiffs_register(&conf);
            }
        }
        if (err != ESP_OK) {
            return err;
        }
    }
    s_mounted = true;
    size_t total = 0U;
    size_t used = 0U;
    if (esp_spiffs_info(STORAGE_PARTITION_LABEL, &total, &used) == ESP_OK) {
        ESP_LOGI(TAG, "storage mounted: %u KB total, %u KB used",
                 (unsigned)(total / 1024U), (unsigned)(used / 1024U));
    }
    return ESP_OK;
}

esp_err_t bsp_storage_unmount(void)
{
    if (!s_mounted) {
        return ESP_OK;
    }
    esp_err_t err = esp_vfs_spiffs_unregister(STORAGE_PARTITION_LABEL);
    s_mounted = (err != ESP_OK);
    return err;
}

bool bsp_storage_is_mounted(void)
{
    return s_mounted;
}

esp_err_t bsp_storage_get_info(uint32_t *total, uint32_t *used)
{
    if ((total == NULL) || (used == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_mounted) {
        return ESP_ERR_INVALID_STATE;
    }
    size_t total_bytes = 0U;
    size_t used_bytes = 0U;
    esp_err_t err = esp_spiffs_info(STORAGE_PARTITION_LABEL, &total_bytes, &used_bytes);
    if (err == ESP_OK) {
        *total = (uint32_t)total_bytes;
        *used = (uint32_t)used_bytes;
    }
    return err;
}

esp_err_t bsp_storage_format(void)
{
    esp_err_t err = esp_spiffs_format(STORAGE_PARTITION_LABEL);
    if (err == ESP_OK) {
        ESP_LOGW(TAG, "storage formatted");
    }
    return err;
}


/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file bsp_sdcard.c
 * @brief SDIO 4-bit SD card mount / unmount / info.
 */

#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "driver/sdmmc_types.h"
#include "esp_vfs_fat.h"
#include "ff.h"
#include "bsp_sdcard.h"

#define TAG "bsp_sdcard"

static sdmmc_card_t *s_card;
static bool s_mounted;

esp_err_t bsp_sdcard_init(void)
{
    ESP_LOGI(TAG, "sdcard support ready (mount deferred)");
    return ESP_OK;
}

esp_err_t bsp_sdcard_mount(void)
{
    if (s_mounted) {
        return ESP_OK;
    }
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    /* S31 的 SDMMC 走 GPIO 矩阵，默认宏展开不可靠：仿官方 korvo BSP
     * 显式指定模块专用 SD 引脚（CLK=IO24/CMD=IO25/D0~D3=IO20~23，
     * PLAN 2.4.2c 管脚表），并启用内部上拉 */
#ifdef CONFIG_SOC_SDMMC_USE_GPIO_MATRIX
    slot.clk = BSP_SD_CLK_GPIO;
    slot.cmd = BSP_SD_CMD_GPIO;
    slot.d0 = BSP_SD_D0_GPIO;
    slot.d1 = BSP_SD_D1_GPIO;
    slot.d2 = BSP_SD_D2_GPIO;
    slot.d3 = BSP_SD_D3_GPIO;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
#endif /* CONFIG_SOC_SDMMC_USE_GPIO_MATRIX */
    esp_vfs_fat_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 8,
        .allocation_unit_size = 16U * 1024U,
        .disk_status_check_enable = true,
        .use_one_fat = false,
    };
    esp_err_t err = esp_vfs_fat_sdmmc_mount(CONFIG_LERO_SD_BASE_PATH, &host, &slot,
                                            &mount_cfg, &s_card);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mount failed (card absent?): %s", esp_err_to_name(err));
        return err;
    }
    s_mounted = true;
    sdmmc_card_print_info(stdout, s_card);
    return ESP_OK;
}

esp_err_t bsp_sdcard_unmount(void)
{
    if (!s_mounted) {
        return ESP_OK;
    }
    esp_err_t err = esp_vfs_fat_sdcard_unmount(CONFIG_LERO_SD_BASE_PATH, s_card);
    s_mounted = (err != ESP_OK);
    return err;
}

bool bsp_sdcard_is_mounted(void)
{
    return s_mounted;
}

esp_err_t bsp_sdcard_get_info(bsp_sdcard_info_t *info)
{
    if (info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_mounted || (s_card == NULL)) {
        return ESP_ERR_INVALID_STATE;
    }
    const uint64_t sector_bytes = (uint64_t)s_card->csd.sector_size;
    const uint64_t total = (uint64_t)s_card->csd.capacity * sector_bytes;

    uint64_t used = total;
    FATFS *fs = NULL;
    DWORD free_clusters = 0U;
    if (f_getfree("", &free_clusters, &fs) == FR_OK) {
        if (fs != NULL) {
            const uint64_t free_bytes = (uint64_t)free_clusters * (uint64_t)fs->csize * sector_bytes;
            used = (free_bytes < total) ? (total - free_bytes) : 0U;
        }
    }

    info->total_bytes = total;
    info->used_bytes = used;
    return ESP_OK;
}

esp_err_t bsp_sdcard_poll(void)
{
    if (s_mounted) {
        return ESP_OK;
    }
    return bsp_sdcard_mount();
}


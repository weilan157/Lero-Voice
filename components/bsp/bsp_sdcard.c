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
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bsp_sdcard.h"

#define TAG "bsp_sdcard"

static sdmmc_card_t *s_card;
static bool s_mounted;

#if CONFIG_LERO_SD_AUTOMOUNT
static StackType_t s_task_stack[CONFIG_LERO_SD_TASK_STACK_SIZE / sizeof(StackType_t)];
static StaticTask_t s_task_tcb;
static TaskHandle_t s_task;
#endif /* CONFIG_LERO_SD_AUTOMOUNT */

static esp_err_t bsp_sdcard_mount_impl(bool verbose);

#if CONFIG_LERO_SD_AUTOMOUNT
static void bsp_sdcard_task(void *arg)
{
    (void)arg;
    for (;;) {
        if (s_mounted && (s_card != NULL)) {
            /* 已挂载：sdmmc_get_status（CMD13）失败视为卡已拔出 */
            if (sdmmc_get_status(s_card) != ESP_OK) {
                ESP_LOGW(TAG, "card removed -> auto unmount");
                (void)bsp_sdcard_unmount();
            }
        } else {
            /* 未挂载：静默尝试（卡插入即挂载成功，失败不刷屏） */
            (void)bsp_sdcard_mount_impl(false);
        }
        vTaskDelay(pdMS_TO_TICKS(CONFIG_LERO_SD_POLL_PERIOD_MS));
    }
}
#endif /* CONFIG_LERO_SD_AUTOMOUNT */

esp_err_t bsp_sdcard_init(void)
{
#if CONFIG_LERO_SD_AUTOMOUNT
    s_task = xTaskCreateStatic(bsp_sdcard_task, "sd_task", sizeof(s_task_stack), NULL,
                               CONFIG_LERO_SD_TASK_PRIORITY, s_task_stack, &s_task_tcb);
    if (s_task == NULL) {
        ESP_LOGE(TAG, "auto-mount task create failed");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "sdcard auto-mount task started (period %d ms)",
             CONFIG_LERO_SD_POLL_PERIOD_MS);
#else
    ESP_LOGI(TAG, "sdcard support ready (mount deferred)");
#endif /* CONFIG_LERO_SD_AUTOMOUNT */
    return ESP_OK;
}

esp_err_t bsp_sdcard_mount(void)
{
    return bsp_sdcard_mount_impl(true);
}

static esp_err_t bsp_sdcard_mount_impl(bool verbose)
{
    if (s_mounted) {
        return ESP_OK;
    }
    /* 挂载参数对齐官方 korvo BSP（esp32_s31_korvo.c bsp_sdcard_mount，
     * 不含其 SD 电源控制）：显式 slot0 / 4-bit / HIGHSPEED(20MHz) /
     * 非对齐多块分块；GPIO 矩阵下显式指定模块专用 SD 引脚
     * （CLK=IO24/CMD=IO25/D0~D3=IO20~23，PLAN 2.4.2c 管脚表） */
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_0;
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
    host.flags &= ~(SDMMC_HOST_FLAG_1BIT | SDMMC_HOST_FLAG_4BIT | SDMMC_HOST_FLAG_8BIT);
    host.flags |= SDMMC_HOST_FLAG_4BIT;
    if ((host.max_freq_khz == SDMMC_FREQ_SDR50) || (host.max_freq_khz == SDMMC_FREQ_SDR104)) {
        host.flags &= ~SDMMC_HOST_FLAG_DDR;
    }
    host.unaligned_multi_block_rw_max_chunk_size = 8;

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 4;
    slot.cd = SDMMC_SLOT_NO_CD;
    slot.wp = SDMMC_SLOT_NO_WP;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    if (host.max_freq_khz > SDMMC_FREQ_HIGHSPEED) {
        slot.flags |= SDMMC_SLOT_FLAG_UHS1;
    }
#ifdef CONFIG_SOC_SDMMC_USE_GPIO_MATRIX
    slot.clk = BSP_SD_CLK_GPIO;
    slot.cmd = BSP_SD_CMD_GPIO;
    slot.d0 = BSP_SD_D0_GPIO;
    slot.d1 = BSP_SD_D1_GPIO;
    slot.d2 = BSP_SD_D2_GPIO;
    slot.d3 = BSP_SD_D3_GPIO;
#endif /* CONFIG_SOC_SDMMC_USE_GPIO_MATRIX */

    esp_vfs_fat_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16U * 1024U,
        /* 官方默认关闭（disk_status mock 常驻）。本项目为无 SD_DET 的
         * 延迟挂载/热插拔方案，开启后 VFS 在文件操作前经 sdmmc_get_status
         * 校验卡在位，拔卡后文件访问会快速失败而非悬挂（PLAN 3.3.1 #6） */
        .disk_status_check_enable = true,
    };
    esp_err_t err = esp_vfs_fat_sdmmc_mount(CONFIG_LERO_SD_BASE_PATH, &host, &slot,
                                            &mount_cfg, &s_card);
    if (err != ESP_OK) {
        if (verbose) {
            ESP_LOGW(TAG, "mount failed (card absent?): %s", esp_err_to_name(err));
        }
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
    /* 拔卡检测：sdmmc_get_status（CMD13）失败即视为卡已拔出，
     * 复位挂载标志并返回错误，避免用缓存的 csd 数据误报容量 */
    if (sdmmc_get_status(s_card) != ESP_OK) {
        s_mounted = false;
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


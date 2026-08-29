/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file bsp_sdcard.c
 * @brief SDIO 4-bit SD card mount / unmount / hot-plug polling.
 *
 * 挂载参数对齐官方 korvo BSP（esp32_s31_korvo.c bsp_sdcard_mount）：
 *   host: slot0 / 4-bit / SDMMC_FREQ_HIGHSPEED(40MHz) /
 *         unaligned_multi_block_rw_max_chunk_size=8 / 清 DDR
 *   slot: width=4 / NO_CD / NO_WP / INTERNAL_PULLUP；GPIO 矩阵显式
 *         CLK=IO24 / CMD=IO25 / D0~D3=IO20~23
 * 无 SD_DET 引脚（PLAN 2.6 #2）：热插拔由 bsp_sdcard_task 周期探测
 * （Kconfig LERO_SD_AUTOMOUNT / POLL_PERIOD_MS，默认 2s）——插卡自动
 * 挂载、拔卡（CMD13 状态失败）自动复位挂载标志并卸载。
 */

#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "driver/sdmmc_types.h"
#include "esp_vfs_fat.h"
#include "ff.h"
#include "bsp_config.h"
#include "bsp_sdcard.h"

#define TAG "bsp_sdcard"

static sdmmc_card_t *s_card;
static volatile bool s_mounted;

/* mount/unmount 互斥：热插拔任务与调用者（player/voice/diag）并发访问 */
static StaticSemaphore_t s_lock_mem;
static SemaphoreHandle_t s_lock;

static TaskHandle_t s_sd_task;
static StaticTask_t s_sd_tcb;
static StackType_t s_sd_stack[CONFIG_LERO_SD_TASK_STACK_SIZE / sizeof(StackType_t)];

/* 拔卡清理：尽力卸载（fatfs 可能因卡不在而失败，标志必须复位） */
static void s_drop_card(void)
{
    (void)esp_vfs_fat_sdcard_unmount(CONFIG_LERO_SD_BASE_PATH, s_card);
    s_card = NULL;
    s_mounted = false;
}

static esp_err_t s_mount_impl(bool verbose)
{
    if (s_mounted) {
        return ESP_OK;
    }
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_0;
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
    host.flags &= ~(SDMMC_HOST_FLAG_1BIT | SDMMC_HOST_FLAG_4BIT | SDMMC_HOST_FLAG_8BIT);
    host.flags |= SDMMC_HOST_FLAG_4BIT;
    host.flags &= ~SDMMC_HOST_FLAG_DDR;     /* 清 DDR（仅 SDR50/104 需要） */
    host.unaligned_multi_block_rw_max_chunk_size = 8;

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
#ifdef CONFIG_SOC_SDMMC_USE_GPIO_MATRIX
    /* S31 SDMMC 走 GPIO 矩阵：显式指定模块专用引脚（官方 korvo 同法） */
    slot.clk = BSP_SD_CLK_GPIO;
    slot.cmd = BSP_SD_CMD_GPIO;
    slot.d0 = BSP_SD_D0_GPIO;
    slot.d1 = BSP_SD_D1_GPIO;
    slot.d2 = BSP_SD_D2_GPIO;
    slot.d3 = BSP_SD_D3_GPIO;
#endif /* CONFIG_SOC_SDMMC_USE_GPIO_MATRIX */
    slot.width = 4;
    slot.cd = SDMMC_SLOT_NO_CD;
    slot.wp = SDMMC_SLOT_NO_WP;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    /* UHS1 仅在 >HIGHSPEED（SDR50/104）时启用（官方 korvo 同逻辑） */

    esp_vfs_fat_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16U * 1024U,
        .disk_status_check_enable = true,
    };
    esp_err_t err = esp_vfs_fat_sdmmc_mount(CONFIG_LERO_SD_BASE_PATH, &host, &slot,
                                            &mount_cfg, &s_card);
    if (err != ESP_OK) {
        if (verbose) {
            ESP_LOGW(TAG, "mount failed (card absent?): %s", esp_err_to_name(err));
        }
        s_card = NULL;
        return err;
    }
    s_mounted = true;
    if (verbose) {
        sdmmc_card_print_info(stdout, s_card);
        ESP_LOGI(TAG, "card mounted at %s", CONFIG_LERO_SD_BASE_PATH);
    }
    return ESP_OK;
}

/* 热插拔探测任务：插卡自动挂载、拔卡自动卸载（无 SD_DET 方案） */
static void s_sdcard_task(void *arg)
{
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS((TickType_t)CONFIG_LERO_SD_POLL_PERIOD_MS));
        if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100U)) != pdTRUE) {
            continue;
        }
        if (s_mounted && (s_card != NULL)) {
            /* CMD13 状态验证卡在位；失败即卡被拔出 */
            if (sdmmc_get_status(s_card) != ESP_OK) {
                ESP_LOGI(TAG, "card removed (CMD13); unmounting");
                s_drop_card();
            }
        } else if (!s_mounted) {
            (void)s_mount_impl(false);      /* 插卡探测：静默，失败不刷屏 */
        }
        (void)xSemaphoreGive(s_lock);
    }
}

esp_err_t bsp_sdcard_init(void)
{
    s_lock = xSemaphoreCreateMutexStatic(&s_lock_mem);
#if CONFIG_LERO_SD_AUTOMOUNT
    s_sd_task = xTaskCreateStaticPinnedToCore(
        s_sdcard_task, "sd_poll",
        (uint32_t)(sizeof(s_sd_stack) / sizeof(StackType_t)), NULL,
        CONFIG_LERO_SD_TASK_PRIORITY, s_sd_stack, &s_sd_tcb, 0);
    if (s_sd_task == NULL) {
        ESP_LOGW(TAG, "sd poll task create failed (fallback: manual poll)");
    }
#endif /* CONFIG_LERO_SD_AUTOMOUNT */
    ESP_LOGI(TAG, "sdcard ready (auto-mount=%d, poll=%u ms)",
             (int)CONFIG_LERO_SD_AUTOMOUNT,
             (unsigned)CONFIG_LERO_SD_POLL_PERIOD_MS);
    return ESP_OK;
}

esp_err_t bsp_sdcard_mount(void)
{
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(2000U)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    const esp_err_t err = s_mount_impl(true);
    (void)xSemaphoreGive(s_lock);
    return err;
}

esp_err_t bsp_sdcard_unmount(void)
{
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(2000U)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = ESP_OK;
    if (s_mounted) {
        err = esp_vfs_fat_sdcard_unmount(CONFIG_LERO_SD_BASE_PATH, s_card);
        s_card = NULL;
        s_mounted = false;
    }
    (void)xSemaphoreGive(s_lock);
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
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(2000U)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    if (!s_mounted || (s_card == NULL)) {
        (void)xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    /* 拔卡验证：CMD13 失败即复位挂载标志（修"拔卡后仍显示"） */
    if (sdmmc_get_status(s_card) != ESP_OK) {
        ESP_LOGW(TAG, "card removed (CMD13)");
        s_drop_card();
        (void)xSemaphoreGive(s_lock);
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
    (void)xSemaphoreGive(s_lock);

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

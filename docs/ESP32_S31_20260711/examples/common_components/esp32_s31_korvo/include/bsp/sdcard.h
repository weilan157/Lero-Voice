/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief ESP32-S31-Korvo BSP SD card interface
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "sdmmc_cmd.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup sdcard SD Card
 *  @brief SDMMC/FATFS BSP API
 *  @{
 */

#define BSP_SDCARD_MOUNT_POINT        "/sdcard"
#define BSP_SDCARD_MAX_FILES          (5)
#define BSP_SDCARD_ALLOC_UNIT_SIZE    (16 * 1024)
#define BSP_SDCARD_BUS_WIDTH          (4)
#define BSP_SDCARD_MAX_FREQ_KHZ       SDMMC_FREQ_HIGHSPEED

typedef struct {
    const char *mount_point;          /*!< Mount point. NULL selects BSP_SDCARD_MOUNT_POINT. */
    bool format_if_mount_failed;      /*!< Format FATFS if mount fails. */
    int max_files;                    /*!< Max open files. 0 selects BSP default. */
    size_t allocation_unit_size;      /*!< FAT allocation unit size. 0 selects BSP default. */
    int max_freq_khz;                 /*!< SDMMC max frequency. 0 selects BSP_SDCARD_MAX_FREQ_KHZ. */
} bsp_sdcard_config_t;

#define BSP_SDCARD_DEFAULT_CONFIG() {                        \
    .mount_point = BSP_SDCARD_MOUNT_POINT,                   \
    .format_if_mount_failed = false,                         \
    .max_files = BSP_SDCARD_MAX_FILES,                       \
    .allocation_unit_size = BSP_SDCARD_ALLOC_UNIT_SIZE,      \
    .max_freq_khz = BSP_SDCARD_MAX_FREQ_KHZ,                 \
}

/**
 * @brief Mount SD card through SDMMC 4-bit bus.
 */
esp_err_t bsp_sdcard_mount(const bsp_sdcard_config_t *config, sdmmc_card_t **out_card);

/**
 * @brief Unmount SD card.
 */
esp_err_t bsp_sdcard_unmount(void);

/**
 * @brief Get mounted SD card handle.
 */
sdmmc_card_t *bsp_sdcard_get_card(void);

/**
 * @brief Get current SD card mount point.
 */
const char *bsp_sdcard_get_mount_point(void);

/** @} */

#ifdef __cplusplus
}
#endif

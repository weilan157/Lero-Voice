/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file bsp_sdcard.h
 * @brief MicroSD card (SDIO 4-bit, module dedicated pins).
 *
 * SD_DET is not wired to the MCU (docs/PLAN.md 2.6 #2): no hot-plug
 * interrupt; use bsp_sdcard_poll() / UI triggered mount (PLAN 3.3.1 #6).
 */

#ifndef BSP_SDCARD_H
#define BSP_SDCARD_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint64_t total_bytes;
    uint64_t used_bytes;
} bsp_sdcard_info_t;

/**
 * @brief Initialize the SDMMC host support (mount is deferred).
 * @return ESP_OK on success.
 */
esp_err_t bsp_sdcard_init(void);

/**
 * @brief Mount the SD card (FAT32) at CONFIG_LERO_SD_BASE_PATH.
 * @return ESP_OK / error (e.g. no card inserted).
 */
esp_err_t bsp_sdcard_mount(void);

/**
 * @brief Unmount the SD card.
 * @return ESP_OK on success.
 */
esp_err_t bsp_sdcard_unmount(void);

/**
 * @brief Check whether the card is currently mounted.
 * @return true if mounted.
 */
bool bsp_sdcard_is_mounted(void);

/**
 * @brief Get total / used space on the mounted card.
 * @param[out] info Filled info record.
 * @return ESP_OK / ESP_ERR_INVALID_STATE when not mounted.
 */
esp_err_t bsp_sdcard_get_info(bsp_sdcard_info_t *info);

/**
 * @brief Poll mount state: if not mounted, try to mount (no hot-plug signal).
 * @return ESP_OK when mounted, error otherwise.
 */
esp_err_t bsp_sdcard_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_SDCARD_H */


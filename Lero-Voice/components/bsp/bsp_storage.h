/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file bsp_storage.h
 * @brief "storage" SPIFFS partition (UI assets / device table / wake word).
 *
 * Never auto-format on mount failure (docs/PLAN.md 8.6 #3); the OTA path
 * never touches this partition.
 */

#ifndef BSP_STORAGE_H
#define BSP_STORAGE_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Mount the storage partition (init; mount failure is non-fatal).
 * @return ESP_OK on success.
 */
esp_err_t bsp_storage_init(void);

/**
 * @brief Mount the SPIFFS storage partition.
 * @return ESP_OK / error.
 */
esp_err_t bsp_storage_mount(void);

/**
 * @brief Unmount the storage partition.
 * @return ESP_OK on success.
 */
esp_err_t bsp_storage_unmount(void);

/**
 * @brief Check whether storage is mounted.
 * @return true if mounted.
 */
bool bsp_storage_is_mounted(void);

/**
 * @brief Get total / used bytes.
 * @param[out] total Total bytes.
 * @param[out] used  Used bytes.
 * @return ESP_OK / ESP_ERR_INVALID_STATE when not mounted.
 */
esp_err_t bsp_storage_get_info(uint32_t *total, uint32_t *used);

/**
 * @brief Format the storage partition (explicit user action only).
 * @return ESP_OK on success.
 */
esp_err_t bsp_storage_format(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_STORAGE_H */


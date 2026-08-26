/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file bsp.h
 * @brief Board support package - the ONLY layer that touches hardware.
 *
 * Application and components must use these APIs instead of touching
 * registers / drivers directly (docs/PLAN.md 3.3).
 */

#ifndef BSP_H
#define BSP_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BSP_MODULE_NAME_MAX     16U
#define BSP_FAULT_BIT_COUNT     BSP_MODULE_COUNT

typedef enum {
    BSP_MODULE_BUTTONS = 0,
    BSP_MODULE_POWER,
    BSP_MODULE_USB,
    BSP_MODULE_AMPLIFIER,
    BSP_MODULE_DISPLAY,
    BSP_MODULE_TOUCH,
    BSP_MODULE_CODEC,
    BSP_MODULE_IMU,
    BSP_MODULE_SDCARD,
    BSP_MODULE_STORAGE,
    BSP_MODULE_COUNT,
} bsp_module_t;

typedef struct {
    char name[BSP_MODULE_NAME_MAX]; /*< 模块名（diag 展示） */
    bool enabled;                   /*< Kconfig 是否启用 */
    bool init_ok;                   /*< 初始化是否成功 */
    esp_err_t last_error;           /*< 最近一次初始化错误码 */
} bsp_module_status_t;

/**
 * @brief Initialize all enabled BSP modules in dependency order.
 *
 * Partial failure is allowed (docs/PLAN.md 3.3.1 #9): each module records its
 * own result; use bsp_get_fault_bitmap() / bsp_get_module_status() to query.
 *
 * @return ESP_OK always (faults are reported through the status API).
 */
esp_err_t bsp_init(void);

/**
 * @brief De-initialize BSP modules (unmount SD/storage, release I2C buses).
 * @return ESP_OK on success.
 */
esp_err_t bsp_deinit(void);

/**
 * @brief Get the init status of one module.
 * @param[in]  module Module id.
 * @param[out] status Filled status record.
 * @return ESP_OK / ESP_ERR_INVALID_ARG.
 */
esp_err_t bsp_get_module_status(bsp_module_t module, bsp_module_status_t *status);

/**
 * @brief Get the fault bitmap: bit N set means module N failed to init.
 * @return Fault bitmap (see bsp_module_t bit positions).
 */
uint32_t bsp_get_fault_bitmap(void);

/**
 * @brief Get the running firmware version (from esp_app_desc).
 * @return NUL terminated version string.
 */
const char *bsp_get_version(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_H */


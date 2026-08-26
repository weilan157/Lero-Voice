/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief ESP32-S31-Korvo BSP button interface
 */

#pragma once

#include "esp_err.h"
#include "iot_button.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup buttons Buttons
 *  @brief ADC button BSP API
 *  @{
 */

typedef enum {
    BSP_BUTTON_VOLUP = 0,      /*!< ADC ladder level: 0.38 V */
    BSP_BUTTON_VOLDOWN,        /*!< ADC ladder level: 0.82 V */
    BSP_BUTTON_MODE,           /*!< ADC ladder level: 1.34 V */
    BSP_BUTTON_SET,            /*!< ADC ladder level: 1.87 V */
    BSP_BUTTON_NUM,
} bsp_button_t;

/**
 * @brief Create all ADC button devices.
 *
 * @param[out] btn_array Output button handle array.
 * @param[out] btn_cnt Number of created buttons. Can be NULL.
 * @param[in] btn_array_size Size of output array. Must be at least BSP_BUTTON_NUM.
 *
 * @return ESP_OK on success, otherwise an ESP-IDF error code.
 */
esp_err_t bsp_iot_button_create(button_handle_t btn_array[], int *btn_cnt, int btn_array_size);

/** @} */

#ifdef __cplusplus
}
#endif

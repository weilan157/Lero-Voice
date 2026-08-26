/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief ESP32-S31-Korvo BSP LED interface
 */

#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup led LED
 *  @brief WS2812 LED BSP API
 *  @{
 */

typedef enum {
    BSP_LED_STATUS = 0,
    BSP_LED_NUM,
} bsp_led_t;

/**
 * @brief Initialize the on-board WS2812 LED.
 */
esp_err_t bsp_led_init(void);

/**
 * @brief Set one LED RGB color and refresh the strip.
 *
 * @param led LED index.
 * @param red Red component.
 * @param green Green component.
 * @param blue Blue component.
 */
esp_err_t bsp_led_set_rgb(bsp_led_t led, uint8_t red, uint8_t green, uint8_t blue);

/**
 * @brief Clear all LEDs.
 */
esp_err_t bsp_led_clear(void);

/** @} */

#ifdef __cplusplus
}
#endif

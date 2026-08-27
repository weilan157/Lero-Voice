/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file bsp_touch.h
 * @brief Capacitive touch controller FT6336U (I2C1: SDA=IO46 / SCL=IO47).
 *
 * FT6336U (FT5x06 protocol family, addr 0x38) driven via
 * espressif/esp_lcd_touch_ft5x06; the handle is exposed for the LVGL adapter
 * (esp_lv_adapter_register_touch). See docs/PLAN.md 2.4.3 / 2.4.2d.
 */

#ifndef BSP_TOUCH_H
#define BSP_TOUCH_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_lcd_touch.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t x;
    uint16_t y;
    bool pressed;
} bsp_touch_point_t;

/**
 * @brief Configure INT/RST GPIOs and create the FT6336U (FT5x06) touch
 *        controller on I2C1 (addr 0x38).
 * @return ESP_OK when the controller was created.
 */
esp_err_t bsp_touch_init(void);

/**
 * @brief Get the esp_lcd_touch handle (NULL until init OK).
 * @return Touch handle or NULL.
 */
esp_lcd_touch_handle_t bsp_touch_get_handle(void);

/**
 * @brief Read the current touch point (via esp_lcd_touch).
 * @param[out] point Filled point.
 * @return ESP_OK / ESP_ERR_NOT_FOUND until the controller is ready.
 */
esp_err_t bsp_touch_read_point(bsp_touch_point_t *point);

#ifdef __cplusplus
}
#endif

#endif /* BSP_TOUCH_H */

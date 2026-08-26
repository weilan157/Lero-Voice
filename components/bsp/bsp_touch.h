/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file bsp_touch.h
 * @brief Capacitive touch controller (I2C1: SDA=IO46 / SCL=IO47).
 *
 * The concrete panel / controller model is still TBD (docs/PLAN.md 11 #1);
 * init scans known addresses and the point API stays a placeholder until the
 * controller driver (e.g. esp_lcd_touch) is integrated.
 */

#ifndef BSP_TOUCH_H
#define BSP_TOUCH_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t x;
    uint16_t y;
    bool pressed;
} bsp_touch_point_t;

/**
 * @brief Configure INT/RST GPIOs and scan for a touch controller on I2C1.
 * @return ESP_OK when at least one candidate address ACKs.
 */
esp_err_t bsp_touch_init(void);

/**
 * @brief Read the current touch point.
 * @param[out] point Filled point.
 * @return ESP_OK / ESP_ERR_NOT_SUPPORTED until the panel is identified.
 */
esp_err_t bsp_touch_read_point(bsp_touch_point_t *point);

#ifdef __cplusplus
}
#endif

#endif /* BSP_TOUCH_H */


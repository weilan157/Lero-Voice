/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file bsp_display.h
 * @brief RGB LCD (18-bit parallel) + backlight PWM.
 *
 * Frame buffers are statically allocated in PSRAM (docs/PLAN.md 3.3.1 #4).
 * Backlight stays OFF after init until the UI renders the first frame
 * (PLAN 3.3.1 #2). Resolution / timing still TBD (PLAN 11 #1) - tune via
 * Kconfig LERO_LCD_*.
 */

#ifndef BSP_DISPLAY_H
#define BSP_DISPLAY_H

#include <stdint.h>
#include "esp_err.h"
#include "esp_lcd_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Init backlight PWM (0%) and the RGB panel.
 * @return ESP_OK on success.
 */
esp_err_t bsp_display_init(void);

/**
 * @brief Get the LCD panel handle (for esp_lvgl_port later).
 * @param[out] panel Returned panel handle.
 * @return ESP_OK / ESP_ERR_INVALID_STATE when not initialized.
 */
esp_err_t bsp_display_get_handle(esp_lcd_panel_handle_t *panel);

/**
 * @brief Get the static frame buffer pointers (for LVGL / UI layer).
 * @param[out] fb0 First frame buffer.
 * @param[out] fb1 Second frame buffer (may be NULL when single buffered).
 * @return ESP_OK / ESP_ERR_INVALID_STATE when not initialized.
 */
esp_err_t bsp_display_get_framebuffers(void **fb0, void **fb1);

/**
 * @brief Get the panel resolution.
 * @param[out] width  Width in pixels.
 * @param[out] height Height in pixels.
 * @return ESP_OK on success.
 */
esp_err_t bsp_display_get_resolution(uint16_t *width, uint16_t *height);

/**
 * @brief Set backlight brightness.
 * @param[in] pct Brightness [0..100].
 * @return ESP_OK on success.
 */
esp_err_t bsp_display_backlight_set(uint8_t pct);

#ifdef __cplusplus
}
#endif

#endif /* BSP_DISPLAY_H */


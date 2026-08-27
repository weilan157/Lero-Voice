/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file ui.h
 * @brief LVGL UI layer (esp_lvgl_adapter).
 */

#ifndef UI_H
#define UI_H

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize LVGL (adapter + display + touch) and start the UI.
 *        Non-blocking: returns once the LVGL task is running.
 * @return ESP_OK on success.
 */
esp_err_t ui_init(void);

/**
 * @brief Get the LVGL display handle created by ui_init().
 * @return Display handle or NULL.
 */
lv_display_t *ui_get_display(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_H */

/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file bsp_buttons.h
 * @brief Functional buttons SW3~5 (docs/PLAN.md 2.4.5 / 3.3).
 *
 * Debounced scanning on a 10 ms esp_timer; events: short / long / very long
 * press. The handler runs in the esp_timer task context - keep it non-blocking.
 */

#ifndef BSP_BUTTONS_H
#define BSP_BUTTONS_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BSP_BTN_ID_1 = 0,
    BSP_BTN_ID_2,
    BSP_BTN_ID_3,
    BSP_BTN_ID_COUNT,
} bsp_button_id_t;

typedef enum {
    BSP_BTN_EVENT_SHORT_PRESS = 0,
    BSP_BTN_EVENT_LONG_PRESS,
    BSP_BTN_EVENT_VERY_LONG_PRESS,
} bsp_button_event_t;

typedef void (*bsp_buttons_cb_t)(bsp_button_id_t button, bsp_button_event_t event);

/**
 * @brief Configure button GPIOs and start the scanning timer.
 * @return ESP_OK on success.
 */
esp_err_t bsp_buttons_init(void);

/**
 * @brief Set the press-event handler (called from esp_timer task context).
 * @param[in] cb Handler, or NULL to clear.
 * @return ESP_OK on success.
 */
esp_err_t bsp_buttons_set_handler(bsp_buttons_cb_t cb);

#ifdef __cplusplus
}
#endif

#endif /* BSP_BUTTONS_H */


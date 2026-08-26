/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file bsp_usb.h
 * @brief USB load switch (USB_EN=IO53, SY6280AAC).
 *
 * Default off to save power (docs/PLAN.md 3.3.1 #8); enable only while the
 * USB function is actually used.
 */

#ifndef BSP_USB_H
#define BSP_USB_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configure the USB_EN GPIO (output low).
 * @return ESP_OK on success.
 */
esp_err_t bsp_usb_init(void);

/**
 * @brief Enable / disable the USB load switch.
 * @param[in] on true = enable USB power.
 * @return ESP_OK on success.
 */
esp_err_t bsp_usb_enable(bool on);

#ifdef __cplusplus
}
#endif

#endif /* BSP_USB_H */


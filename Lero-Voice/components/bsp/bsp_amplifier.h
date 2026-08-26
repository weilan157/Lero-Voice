/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file bsp_amplifier.h
 * @brief NS4150B class-D amplifier control (PA_CTRL=IO52).
 *
 * Sequencing (docs/PLAN.md 3.3.1 #3): codec init first, PA stays muted;
 * enable PA only right before the first playback. Mute before source switch.
 */

#ifndef BSP_AMPLIFIER_H
#define BSP_AMPLIFIER_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configure PA_CTRL GPIO (output low, muted).
 * @return ESP_OK on success.
 */
esp_err_t bsp_amp_init(void);

/**
 * @brief Enable / disable the amplifier power stage.
 * @param[in] on true = PA powered on (unless muted).
 * @return ESP_OK on success.
 */
esp_err_t bsp_amp_enable(bool on);

/**
 * @brief Mute / unmute the amplifier.
 * @param[in] mute true = force PA off.
 * @return ESP_OK on success.
 */
esp_err_t bsp_amp_mute(bool mute);

#ifdef __cplusplus
}
#endif

#endif /* BSP_AMPLIFIER_H */


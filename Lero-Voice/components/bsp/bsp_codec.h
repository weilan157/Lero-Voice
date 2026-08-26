/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file bsp_codec.h
 * @brief ES8389 audio codec (I2C0, addr 0x20).
 *
 * The playback/record pipeline belongs to esp_codec_dev (managed component,
 * docs/PLAN.md 3.6 / 6). This module owns the I2C probe and keeps the BSP
 * interface stable so the codec driver can be swapped internally without
 * touching callers (high cohesion / low coupling).
 */

#ifndef BSP_CODEC_H
#define BSP_CODEC_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Probe the codec on I2C0 (address 0x20).
 * @return ESP_OK when the codec answers.
 */
esp_err_t bsp_codec_init(void);

/**
 * @brief Check whether the codec was found.
 * @return true when present.
 */
bool bsp_codec_is_present(void);

/**
 * @brief Set playback volume.
 * @param[in] volume_pct Volume [0..100].
 * @return ESP_OK / ESP_ERR_NOT_SUPPORTED until esp_codec_dev is integrated.
 */
esp_err_t bsp_codec_set_volume(uint8_t volume_pct);

/**
 * @brief Mute / unmute the codec output.
 * @param[in] mute true = muted.
 * @return ESP_OK / ESP_ERR_NOT_SUPPORTED until esp_codec_dev is integrated.
 */
esp_err_t bsp_codec_mute(bool mute);

#ifdef __cplusplus
}
#endif

#endif /* BSP_CODEC_H */


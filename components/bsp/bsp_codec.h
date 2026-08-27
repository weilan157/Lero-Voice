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
#include "esp_codec_dev.h"

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
 * @brief Get the esp_codec_dev playback/record handle (NULL until init OK).
 * @return Codec device handle or NULL.
 */
esp_codec_dev_handle_t bsp_codec_get_handle(void);

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

/**
 * @brief Read one ES8389 register over I2C0 (debug / console "codec").
 * @param[in]  reg Register address (0x00..0xFF).
 * @param[out] val Read value.
 * @return ESP_OK / ESP_ERR_INVALID_STATE until the codec is probed.
 */
esp_err_t bsp_codec_read_reg(uint8_t reg, uint8_t *val);

#ifdef __cplusplus
}
#endif

#endif /* BSP_CODEC_H */


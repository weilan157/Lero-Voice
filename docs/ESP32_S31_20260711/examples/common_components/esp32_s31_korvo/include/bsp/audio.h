/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief ESP32-S31-Korvo BSP audio interface
 */

#pragma once

#include "driver/i2s_std.h"
#include "esp_err.h"
#include "esp_codec_dev.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup audio Audio
 *  @brief ES8389 codec BSP API
 *  @{
 */

#define BSP_AUDIO_DEFAULT_SAMPLE_RATE     (48000)
#define BSP_AUDIO_DEFAULT_BITS_PER_SAMPLE (16)
#define BSP_AUDIO_DEFAULT_CHANNELS        (2)

/**
 * @brief Initialize I2S channels used by the ES8389 codec.
 *
 * @param[in] i2s_config I2S standard mode configuration. NULL selects BSP default stereo 48 kHz.
 *
 * @return ESP_OK on success, otherwise an ESP-IDF error code.
 */
esp_err_t bsp_audio_init(const i2s_std_config_t *i2s_config);

/**
 * @brief Create speaker codec device.
 *
 * The returned handle should be used with esp_codec_dev_open(), esp_codec_dev_write(),
 * esp_codec_dev_set_out_vol(), and esp_codec_dev_close().
 */
esp_codec_dev_handle_t bsp_audio_codec_speaker_init(void);

/**
 * @brief Create microphone codec device.
 *
 * The returned handle should be used with esp_codec_dev_open(), esp_codec_dev_read(),
 * esp_codec_dev_set_in_gain(), and esp_codec_dev_close().
 */
esp_codec_dev_handle_t bsp_audio_codec_microphone_init(void);

/** @} */

#ifdef __cplusplus
}
#endif

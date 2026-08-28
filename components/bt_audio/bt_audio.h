/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file bt_audio.h
 * @brief Bluetooth Classic A2DP sink — stream music from a phone to ES8389.
 *
 * S31 supports BT Classic (IDF commit 11268d8). Bluedroid decodes SBC
 * internally and delivers PCM through the legacy sink data callback
 * (esp_a2dp_legacy_api.h); the PCM is written straight to the shared
 * esp_codec_dev (I2S -> ES8389 -> NS4150B).
 *
 * Coexistence: BT playback and the local player are mutually exclusive —
 * starting BT streaming stops the local player (player_stop) and takes
 * over the codec; stopping the stream releases it again.
 */

#ifndef BT_AUDIO_H
#define BT_AUDIO_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool enabled;       /*< 蓝牙已初始化并可被发现 */
    bool connected;     /*< 已与手机建立 A2DP 连接 */
    bool streaming;     /*< 正在传输音频（AUDIO_STATE STARTED） */
    bool codec_open;    /*< ES8389 已被 BT 音频占用 */
    uint32_t sample_rate; /*< 当前流采样率（0=未知） */
} bt_audio_state_t;

/**
 * @brief Initialize BT Classic controller + Bluedroid + A2DP sink.
 *        Device becomes discoverable/connectable (phone pairs and plays).
 *        Failures are logged and return an error (non-fatal to the system).
 * @return ESP_OK on success.
 */
esp_err_t bt_audio_init(void);

/**
 * @brief Query the current BT audio state (console "bt").
 * @param[out] state Returned state.
 * @return ESP_OK on success.
 */
esp_err_t bt_audio_get_state(bt_audio_state_t *state);

#ifdef __cplusplus
}
#endif

#endif /* BT_AUDIO_H */

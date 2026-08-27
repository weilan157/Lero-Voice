/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file voice.h
 * @brief Voice assistant skeleton (docs/PLAN.md 3.9, M9).
 *
 * Layering:  wake (ESP-SR, pending) / capture (I2S->ES8389 ADC) / VAD /
 *            transport (upload to cloud ASR/LLM) / playback (audio focus).
 *
 * Current skeleton scope:
 *   - voice_task (priority 15, Core 1, static) owns the capture state machine
 *   - voice_capture: 48 kHz / 2 ch / 16 bit PCM frames via esp_codec_dev
 *     (shared duplex codec with the player; clock domain must match)
 *   - voice_transport: pluggable upload interface; null transport ships as
 *     the default (logging + stats), to be replaced by ESP Private Agents /
 *     self-built WebSocket in M9
 *   - voice_wake: ESP-SR WakeNet integration point (S31 support pending);
 *     v1 is triggered by buttons / events -> voice_listen_start()
 *
 * TTS playback is routed through the audio focus arbitration (player / M9).
 */

#ifndef VOICE_H
#define VOICE_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VOICE_STATE_IDLE = 0,       /*< 空闲 */
    VOICE_STATE_LISTENING,      /*< 聆听中（采集 + VAD + 上传） */
    VOICE_STATE_RECORDING,      /*< 录音中（写 WAV 到 SD，console: rec） */
    VOICE_STATE_PROCESSING,     /*< 云端处理中（M9） */
    VOICE_STATE_SPEAKING,       /*< TTS 播报中（M9） */
} voice_state_t;

typedef void (*voice_event_cb_t)(voice_state_t state, const char *info);

/**
 * @brief Initialize the voice component (capture, transport, task).
 * @return ESP_OK on success.
 */
esp_err_t voice_init(void);

/**
 * @brief Start a listen session (wake word / button / event triggered).
 * @return ESP_OK when the session was scheduled.
 */
esp_err_t voice_listen_start(void);

/**
 * @brief Stop the current listen session (manual / event).
 * @return ESP_OK on success.
 */
esp_err_t voice_listen_stop(void);

/**
 * @brief Record audio for a fixed duration into a WAV file on the SD card,
 *        then play it back automatically (console test command "rec").
 * @param[in] seconds  Recording duration (1..600). 0 = use Kconfig default.
 * @param[in] path     Output WAV path, absolute SD path (e.g.
 *                     "/sdcard/record/rec.wav"). NULL = Kconfig default.
 * @return ESP_OK when the recording session was scheduled.
 */
esp_err_t voice_record_start(uint32_t seconds, const char *path);

/**
 * @brief Stop the current recording session early (keeps the WAV file).
 * @return ESP_OK on success.
 */
esp_err_t voice_record_stop(void);

/**
 * @brief Get the current voice state.
 * @param[out] state Returned state.
 * @return ESP_OK on success.
 */
esp_err_t voice_get_state(voice_state_t *state);

/**
 * @brief Register a state-change callback (may be NULL).
 */
void voice_register_event_cb(voice_event_cb_t cb);

#ifdef __cplusplus
}
#endif

#endif /* VOICE_H */

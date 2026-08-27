/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file player.h
 * @brief SD card audio player (docs/PLAN.md 6.2).
 *
 * Built on espressif/esp_audio_simple_player (ESP-GMF): decodes MP3 / WAV /
 * FLAC / AAC / AMR / M4A / OPUS from a local file URI and streams PCM to the
 * ES8389 codec through esp_codec_dev. The decoder/transformer pipeline runs
 * in the component's own task; all APIs are thread-safe wrappers.
 *
 * Playback is async: player_play_file() returns once the pipeline started;
 * completion / errors are reported through the optional event callback.
 */

#ifndef PLAYER_H
#define PLAYER_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PLAYER_STATE_IDLE = 0,      /*< 未在播放 */
    PLAYER_STATE_PLAYING,       /*< 播放中 */
    PLAYER_STATE_PAUSED,        /*< 暂停 */
    PLAYER_STATE_FINISHED,      /*< 播放完成（自动停止） */
    PLAYER_STATE_ERROR,         /*< 播放出错 */
} player_state_t;

typedef void (*player_event_cb_t)(player_state_t state, const char *uri);

/**
 * @brief Initialize the player (creates the ESP-GMF pipeline task).
 * @return ESP_OK on success.
 */
esp_err_t player_init(void);

/**
 * @brief Play an audio file from the SD card.
 * @param[in] path  File path: absolute ("/sdcard/audio/x.mp3") or relative
 *                  to the SD mount root ("audio/x.mp3"). Format is chosen
 *                  from the file extension (mp3/wav/flac/aac/...).
 * @return ESP_OK when the pipeline started, error otherwise.
 */
esp_err_t player_play_file(const char *path);

/**
 * @brief Play an audio file from the SD card in loop mode (repeat forever).
 *        Playback restarts automatically on FINISHED until player_stop().
 * @param[in] path  Same convention as player_play_file().
 * @return ESP_OK when the pipeline started, error otherwise.
 */
esp_err_t player_play_loop(const char *path);

/**
 * @brief Stream an audio file over HTTP(S) and play it in loop mode.
 *        No SD card required: the esp_audio_simple_player HTTP IO stream
 *        decodes and plays the network stream on the fly (MP3/WAV/FLAC/...).
 *        On FINISHED the same URL is requested again until player_stop().
 * @param[in] url  Full HTTP(S) URL, e.g. "http://192.168.1.10:8000/song.mp3".
 * @return ESP_OK when the pipeline started, error otherwise.
 */
esp_err_t player_play_http(const char *url);

/**
 * @brief Download an audio file from a HTTP(S) URL to the SD card, then
 *        play it in loop mode (async; returns after the download task starts).
 * @param[in] url  HTTP(S) URL of the audio file (e.g. https://.../song.mp3).
 * @return ESP_OK when the download task was scheduled, error otherwise.
 */
esp_err_t player_play_url(const char *url);

/**
 * @brief Stop playback and release the codec.
 * @return ESP_OK on success.
 */
esp_err_t player_stop(void);

/**
 * @brief Pause playback.
 * @return ESP_OK on success.
 */
esp_err_t player_pause(void);

/**
 * @brief Resume paused playback.
 * @return ESP_OK on success.
 */
esp_err_t player_resume(void);

/**
 * @brief Set playback volume.
 * @param[in] pct Volume [0..100].
 * @return ESP_OK on success.
 */
esp_err_t player_set_volume(uint8_t pct);

/**
 * @brief Get the current player state.
 * @param[out] state Returned state.
 * @return ESP_OK on success.
 */
esp_err_t player_get_state(player_state_t *state);

/**
 * @brief Register a state-change callback (may be NULL to disable).
 * @param[in] cb Callback invoked from the player task context.
 */
void player_register_event_cb(player_event_cb_t cb);

#ifdef __cplusplus
}
#endif

#endif /* PLAYER_H */

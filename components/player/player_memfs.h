/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file player_memfs.h
 * @brief In-RAM virtual file system for WAV playback without SD card.
 */

#ifndef PLAYER_MEMFS_H
#define PLAYER_MEMFS_H

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register the "/mem" VFS mount (idempotent).
 * @return ESP_OK on success.
 */
esp_err_t player_memfs_init(void);

/**
 * @brief Point the in-RAM file "rec.wav" at a buffer (WAV bytes with header).
 *        Must be called before starting playback; the buffer must stay valid
 *        for the whole playback session (no copy is made).
 * @param[in] data  WAV data buffer.
 * @param[in] size  Valid bytes in the buffer (>= 44).
 * @return ESP_OK on success.
 */
esp_err_t player_memfs_set_data(const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* PLAYER_MEMFS_H */

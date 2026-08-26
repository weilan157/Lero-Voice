/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef VOICE_INTERNAL_H
#define VOICE_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "voice.h"

#ifdef __cplusplus
extern "C" {
#endif

/* voice.c */
void voice_set_state(voice_state_t state, const char *info);

/* voice_capture.c */
typedef struct {
    bool utterance;             /* 检测到有效语音段 */
    uint32_t frames;            /* 采集帧数 */
    uint32_t bytes;             /* 采集字节数 */
} voice_capture_result_t;

esp_err_t voice_capture_init(void);
/* 运行一次聆听会话（阻塞式，直至 stop/超时/端点）；*stop 由外部置位 */
voice_capture_result_t voice_capture_run(volatile bool *stop);

/* voice_transport.c */
esp_err_t voice_transport_init(void);
esp_err_t voice_transport_send(const int16_t *pcm, size_t frames,
                               uint32_t sample_rate, uint8_t channels);

/* voice_wake.c */
esp_err_t voice_wake_init(void);

#ifdef __cplusplus
}
#endif

#endif /* VOICE_INTERNAL_H */

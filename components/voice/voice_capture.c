/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file voice_capture.c
 * @brief Mic capture via esp_codec_dev (ES8389 ADC, dual mic) + simple VAD.
 *
 * The codec is shared duplex with the player (esp_codec_dev IN_OUT handle);
 * the I2S clock domain forces one sample rate, so capture uses the same
 * 48 kHz / 2 ch / 16 bit format as playback (docs/PLAN.md 3.5.3 rule 2).
 * Cloud ASR either accepts 48 kHz or resamples server-side (M9 detail).
 *
 * VAD v1: RMS threshold + tail timeout; enough for skeleton/telemetry.
 * Replace with ESP-SR AFE (AEC/NS) when the S31 support matrix allows.
 */

#include <string.h>
#include <stdlib.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_codec_dev.h"
#include "bsp_codec.h"
#include "voice_internal.h"

#define TAG "voice_capture"

#define FRAME_SAMPLES \
    ((CONFIG_LERO_VOICE_SAMPLE_RATE * CONFIG_LERO_VOICE_CHANNELS * CONFIG_LERO_VOICE_FRAME_MS) / 1000)
#define FRAME_BYTES    (FRAME_SAMPLES * 2U)

static esp_codec_dev_handle_t s_codec;
static int16_t s_pcm[FRAME_SAMPLES];

esp_err_t voice_capture_init(void)
{
    s_codec = bsp_codec_get_handle();
    if (s_codec == NULL) {
        ESP_LOGE(TAG, "codec not ready (bsp_codec init failed?)");
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "capture ready (%d Hz / %d ch / 16 bit, frame %d ms)",
             CONFIG_LERO_VOICE_SAMPLE_RATE, CONFIG_LERO_VOICE_CHANNELS,
             CONFIG_LERO_VOICE_FRAME_MS);
    return ESP_OK;
}

/* 简化 VAD：帧 RMS 超阈值即视为语音 */
static bool s_frame_has_speech(const int16_t *pcm, size_t samples)
{
    uint64_t sum = 0U;
    for (size_t i = 0U; i < samples; i++) {
        const int32_t v = (int32_t)pcm[i];
        sum += (uint64_t)(v * v);
    }
    if (samples == 0U) {
        return false;
    }
    const uint64_t mean = sum / (uint64_t)samples;
    /* sqrt 太贵；用均值近似（阈值按 RMS 标定） */
    const uint64_t approx = mean / 1000U;
    return (approx >= (uint64_t)CONFIG_LERO_VOICE_VAD_RMS_THRESHOLD);
}

voice_capture_result_t voice_capture_run(volatile bool *stop)
{
    voice_capture_result_t res;
    (void)memset(&res, 0, sizeof(res));

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = CONFIG_LERO_VOICE_SAMPLE_RATE,
        .channel = CONFIG_LERO_VOICE_CHANNELS,
        .bits_per_sample = 16,
    };
    /* 播放器可能已打开同格式 codec：打开失败时直接读（双工共享） */
    const bool opened = (esp_codec_dev_open(s_codec, &fs) == ESP_CODEC_DEV_OK);
    if (!opened) {
        ESP_LOGD(TAG, "codec already open (player active); reading shared duplex");
    }

    const int64_t start_us = esp_timer_get_time();
    const int64_t timeout_us = (int64_t)CONFIG_LERO_VOICE_LISTEN_TIMEOUT_MS * 1000;
    const int64_t tail_us = (int64_t)CONFIG_LERO_VOICE_VAD_TAIL_MS * 1000;
    const int64_t min_us = (int64_t)CONFIG_LERO_VOICE_MIN_UTTERANCE_MS * 1000;
    int64_t last_speech_us = 0;

    while ((stop == NULL) || !(*stop)) {
        if ((esp_timer_get_time() - start_us) >= timeout_us) {
            break;
        }
        const int got = esp_codec_dev_read(s_codec, s_pcm, FRAME_BYTES);
        if (got <= 0) {
            vTaskDelay(pdMS_TO_TICKS(2U));  /* 无数据：稍候重试 */
            continue;
        }
        const size_t samples = (size_t)got / 2U;
        res.frames++;
        res.bytes += (uint32_t)got;

        const bool speech = s_frame_has_speech(s_pcm, samples);
        if (speech) {
            res.utterance = true;
            last_speech_us = esp_timer_get_time();
        }
        /* 上传（transport 空实现只统计；M9 替换为真实通道） */
        if (res.utterance) {
            (void)voice_transport_send(s_pcm, samples / (size_t)CONFIG_LERO_VOICE_CHANNELS,
                                       CONFIG_LERO_VOICE_SAMPLE_RATE,
                                       CONFIG_LERO_VOICE_CHANNELS);
        }
        /* 端点检测：语音后静音超尾长（且已过最短语句时长）→ 结束 */
        if (res.utterance && (last_speech_us != 0) &&
            ((esp_timer_get_time() - last_speech_us) >= tail_us) &&
            ((esp_timer_get_time() - start_us) >= min_us)) {
            break;
        }
    }

    if (opened) {
        (void)esp_codec_dev_close(s_codec);
    }
    return res;
}

/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file voice_transport.c
 * @brief Upload transport interface + default null transport.
 *
 * M9 replaces the null transport with a real channel:
 *   - ESP Private Agents (esp-agent) or
 *   - self-built WebSocket pipeline (ASR -> LLM -> TTS streaming).
 *
 * The transport contract keeps voice_capture decoupled from the cloud
 * provider; key material must live in a gateway / platform, never plaintext
 * on the device (docs/PLAN.md 3.9).
 */

#include <string.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "voice_internal.h"

#define TAG "voice_transport"

typedef struct {
    esp_err_t (*open)(void *ctx);
    esp_err_t (*send)(void *ctx, const int16_t *pcm, size_t frames,
                      uint32_t sample_rate, uint8_t channels);
    esp_err_t (*close)(void *ctx);
} voice_transport_if_t;

static const voice_transport_if_t *s_if;
static void *s_ctx;
static uint32_t s_frames_total;

/* ---------------- 默认空实现：仅统计与日志 ---------------- */

static esp_err_t s_null_open(void *ctx)
{
    (void)ctx;
    ESP_LOGI(TAG, "null transport open (replace in M9: Private Agents / WS)");
    return ESP_OK;
}

static esp_err_t s_null_send(void *ctx, const int16_t *pcm, size_t frames,
                             uint32_t sample_rate, uint8_t channels)
{
    (void)ctx;
    (void)pcm;
    s_frames_total += (uint32_t)frames;
    if ((s_frames_total % 5000U) < 100U) {      /* 周期性打点，避免刷屏 */
        ESP_LOGD(TAG, "null transport: %lu frames @ %lu Hz / %u ch",
                 (unsigned long)s_frames_total, (unsigned long)sample_rate,
                 (unsigned)channels);
    }
    return ESP_OK;
}

static esp_err_t s_null_close(void *ctx)
{
    (void)ctx;
    ESP_LOGI(TAG, "null transport close (total %lu frames)",
             (unsigned long)s_frames_total);
    return ESP_OK;
}

static const voice_transport_if_t s_null_if = {
    .open = s_null_open,
    .send = s_null_send,
    .close = s_null_close,
};

/* ---------------- 接口 ---------------- */

esp_err_t voice_transport_init(void)
{
    s_if = &s_null_if;
    s_ctx = NULL;
    s_frames_total = 0U;
    (void)s_if->open(s_ctx);
    ESP_LOGI(TAG, "transport ready (null)");
    return ESP_OK;
}

esp_err_t voice_transport_send(const int16_t *pcm, size_t frames,
                               uint32_t sample_rate, uint8_t channels)
{
    if (s_if == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return s_if->send(s_ctx, pcm, frames, sample_rate, channels);
}

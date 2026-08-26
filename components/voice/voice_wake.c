/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file voice_wake.c
 * @brief Wake word / AFE integration point (docs/PLAN.md 3.9, M9).
 *
 * ESP-SR (WakeNet / MultiNet / AFE) support on ESP32-S31 is pending
 * confirmation of the support matrix. Until then:
 *   - listen sessions are triggered by buttons / events (voice_listen_start)
 *   - AEC/NS 前端处理由 ESP-SR AFE 提供（若可用），否则用播放参考信号兜底
 *
 * Once WakeNet is available, its callback calls voice_listen_start().
 */

#include "esp_log.h"
#include "voice_internal.h"

#define TAG "voice_wake"

esp_err_t voice_wake_init(void)
{
    ESP_LOGI(TAG, "wake init (ESP-SR pending S31 support; button-triggered)");
    return ESP_OK;
}

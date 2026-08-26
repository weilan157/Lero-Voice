/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file voice.c
 * @brief Voice state machine + voice_task (docs/PLAN.md 3.5.1 / 3.9).
 *
 * voice_task (priority 15, Core 1, static stack) serializes listen sessions:
 *   IDLE --listen_start--> LISTENING (capture+VAD+transport)
 *                          --utterance done / stop / timeout--> IDLE
 * PROCESSING / SPEAKING states are reserved for the M9 cloud pipeline
 * (ASR -> LLM -> TTS), triggered from the transport / event layer.
 */

#include <string.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "voice.h"
#include "voice_internal.h"

#define TAG "voice"

static voice_state_t s_state;
static voice_event_cb_t s_cb;
static volatile bool s_listen_request;
static volatile bool s_listen_stop;

static StackType_t s_voice_stack[CONFIG_LERO_VOICE_TASK_STACK / sizeof(StackType_t)];
static StaticTask_t s_voice_tcb;
static bool s_task_started;

void voice_set_state(voice_state_t state, const char *info)
{
    s_state = state;
    ESP_LOGI(TAG, "state=%d (%s)", (int)state, (info != NULL) ? info : "-");
    if (s_cb != NULL) {
        s_cb(state, info);
    }
}

static void s_voice_task(void *arg)
{
    (void)arg;
    for (;;) {
        if (s_listen_request) {
            s_listen_request = false;
            s_listen_stop = false;
            voice_set_state(VOICE_STATE_LISTENING, "listen start");

            const voice_capture_result_t res = voice_capture_run(&s_listen_stop);
            s_listen_stop = false;

            const char *why = "timeout";
            if (res.utterance) {
                why = "utterance done";
            } else if (res.frames == 0U) {
                why = "capture failed";
            }
            voice_set_state(VOICE_STATE_IDLE, why);
            ESP_LOGI(TAG, "session: frames=%lu bytes=%lu utterance=%d",
                     (unsigned long)res.frames, (unsigned long)res.bytes,
                     (int)res.utterance);
        }
        vTaskDelay(pdMS_TO_TICKS(50U));
    }
}

esp_err_t voice_init(void)
{
    if (s_task_started) {
        return ESP_OK;
    }
    esp_err_t err = voice_wake_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "wake init failed: %s", esp_err_to_name(err));
    }
    err = voice_capture_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "capture init failed: %s", esp_err_to_name(err));
        return err;
    }
    err = voice_transport_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "transport init failed: %s", esp_err_to_name(err));
        return err;
    }

    xTaskCreateStaticPinnedToCore(s_voice_task, "voice_task", sizeof(s_voice_stack), NULL,
                                  CONFIG_LERO_VOICE_TASK_PRIORITY,
                                  s_voice_stack, &s_voice_tcb, CONFIG_LERO_VOICE_TASK_CORE);
    s_task_started = true;
    voice_set_state(VOICE_STATE_IDLE, "init");
    ESP_LOGI(TAG, "voice ready (prio=%d core=%d)",
             CONFIG_LERO_VOICE_TASK_PRIORITY, CONFIG_LERO_VOICE_TASK_CORE);
    return ESP_OK;
}

esp_err_t voice_listen_start(void)
{
    if (!s_task_started) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_state == VOICE_STATE_LISTENING) {
        return ESP_OK;                      /* 已在聆听 */
    }
    s_listen_request = true;
    return ESP_OK;
}

esp_err_t voice_listen_stop(void)
{
    s_listen_stop = true;                   /* 采集循环每帧检查 */
    return ESP_OK;
}

esp_err_t voice_get_state(voice_state_t *state)
{
    if (state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *state = s_state;
    return ESP_OK;
}

void voice_register_event_cb(voice_event_cb_t cb)
{
    s_cb = cb;
}

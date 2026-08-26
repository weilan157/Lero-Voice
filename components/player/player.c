/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file player.c
 * @brief SD card audio player implementation (docs/PLAN.md 6.2).
 *
 * Pipeline: file://sdcard/... -> esp_audio_simple_player (ESP-GMF decoders +
 * transformers) -> PCM out callback -> esp_codec_dev (ES8389 via I2S).
 *
 * The codec is opened on ESP_ASP_EVENT_TYPE_MUSIC_INFO (sample rate /
 * channels / bits from the decoded stream) and closed on STOPPED / FINISHED /
 * ERROR. The PA (NS4150B) is enabled only while a stream is active
 * (docs/PLAN.md 3.3.1 #3, no pop at boot).
 */

#include <string.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_audio_simple_player.h"
#include "esp_codec_dev.h"
#include "bsp_codec.h"
#include "bsp_amplifier.h"
#include "bsp_sdcard.h"
#include "player.h"

#define TAG "player"

#define PLAYER_URI_MAX      192U
#define PLAYER_URI_PREFIX   "file://sdcard/"

static esp_asp_handle_t s_player;
static esp_codec_dev_handle_t s_codec;
static bool s_codec_open;
static player_state_t s_state;
static player_event_cb_t s_cb;

/* ------------------------------------------------------------------------- */
/* PCM / event callbacks (run in the ESP-GMF player task context)            */
/* ------------------------------------------------------------------------- */

static int s_pcm_out(uint8_t *data, int data_size, void *ctx)
{
    (void)ctx;
    if ((s_codec == NULL) || !s_codec_open || (data_size <= 0)) {
        return 0;                   /* 未就绪时丢弃，避免写未打开设备 */
    }
    (void)esp_codec_dev_write(s_codec, data, (size_t)data_size);
    return 0;
}

static void s_finish(player_state_t state)
{
    if (s_codec_open) {
        (void)esp_codec_dev_close(s_codec);
        s_codec_open = false;
    }
    (void)bsp_amp_enable(false);    /* 播放结束关闭功放（防噪） */
    s_state = state;
    if (s_cb != NULL) {
        s_cb(state, NULL);
    }
}

static int s_event(esp_asp_event_pkt_t *event, void *ctx)
{
    (void)ctx;
    if (event == NULL) {
        return 0;
    }
    if (event->type == ESP_ASP_EVENT_TYPE_MUSIC_INFO) {
        esp_asp_music_info_t info;
        (void)memset(&info, 0, sizeof(info));
        if (event->payload_size > (int)sizeof(info)) {
            event->payload_size = (int)sizeof(info);
        }
        (void)memcpy(&info, event->payload, (size_t)event->payload_size);
        ESP_LOGI(TAG, "music info: %d Hz, %d ch, %d bit",
                 (int)info.sample_rate, (int)info.channels, (int)info.bits);
        if ((s_codec != NULL) && !s_codec_open) {
            esp_codec_dev_sample_info_t fs = {
                .sample_rate = info.sample_rate,
                .channel = info.channels,
                .bits_per_sample = info.bits,
            };
            if (esp_codec_dev_open(s_codec, &fs) == ESP_CODEC_DEV_OK) {
                s_codec_open = true;
                (void)bsp_amp_enable(true);
            } else {
                ESP_LOGE(TAG, "codec open failed");
            }
        }
    } else if (event->type == ESP_ASP_EVENT_TYPE_STATE) {
        esp_asp_state_t st = ESP_ASP_STATE_IDLE;
        if (event->payload_size >= (int)sizeof(st)) {
            (void)memcpy(&st, event->payload, sizeof(st));
        }
        switch (st) {
        case ESP_ASP_STATE_RUNNING:
            s_state = PLAYER_STATE_PLAYING;
            if (s_cb != NULL) {
                s_cb(s_state, NULL);
            }
            break;
        case ESP_ASP_STATE_PAUSED:
            s_state = PLAYER_STATE_PAUSED;
            if (s_cb != NULL) {
                s_cb(s_state, NULL);
            }
            break;
        case ESP_ASP_STATE_STOPPED:
        case ESP_ASP_STATE_FINISHED:
            s_finish(PLAYER_STATE_FINISHED);
            break;
        case ESP_ASP_STATE_ERROR:
            ESP_LOGE(TAG, "player error");
            s_finish(PLAYER_STATE_ERROR);
            break;
        default:
            break;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Public API                                                                */
/* ------------------------------------------------------------------------- */

esp_err_t player_init(void)
{
    if (s_player != NULL) {
        return ESP_OK;
    }
    s_codec = bsp_codec_get_handle();
    if (s_codec == NULL) {
        ESP_LOGE(TAG, "codec not ready (bsp_codec init failed?)");
        return ESP_ERR_NOT_FOUND;
    }
    esp_asp_cfg_t cfg = {
        .out.cb = s_pcm_out,
        .out.user_ctx = NULL,
        .task_prio = CONFIG_LERO_PLAYER_TASK_PRIORITY,
        .task_stack = CONFIG_LERO_PLAYER_TASK_STACK,
    };
    esp_gmf_err_t err = esp_audio_simple_player_new(&cfg, &s_player);
    if (err != ESP_GMF_ERR_OK) {
        ESP_LOGE(TAG, "simple player new failed: %d", (int)err);
        return ESP_FAIL;
    }
    err = esp_audio_simple_player_set_event(s_player, s_event, NULL);
    if (err != ESP_GMF_ERR_OK) {
        ESP_LOGE(TAG, "set event failed: %d", (int)err);
        return ESP_FAIL;
    }
    s_state = PLAYER_STATE_IDLE;
    (void)player_set_volume((uint8_t)CONFIG_LERO_PLAYER_DEFAULT_VOLUME);
    ESP_LOGI(TAG, "player ready (esp_audio_simple_player)");
    return ESP_OK;
}

static esp_err_t s_build_uri(const char *path, char *uri, size_t uri_size)
{
    if ((path == NULL) || (uri == NULL) || (uri_size == 0U)) {
        return ESP_ERR_INVALID_ARG;
    }
    const char *rel = path;
    if (rel[0] == '/') {
        rel++;                          /* 去掉绝对路径前导 '/' */
    }
    if (strncmp(rel, "sdcard/", 7U) == 0) {
        rel += 7U;                      /* 兼容 "sdcard/..." 写法 */
    }
    if (rel[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if ((strlen(PLAYER_URI_PREFIX) + strlen(rel) + 1U) > uri_size) {
        return ESP_ERR_INVALID_SIZE;
    }
    (void)snprintf(uri, uri_size, "%s%s", PLAYER_URI_PREFIX, rel);
    return ESP_OK;
}

esp_err_t player_play_file(const char *path)
{
    if (s_player == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = bsp_sdcard_poll();          /* 挂载 SD（延迟挂载 + 轮询） */
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SD unavailable: %s", esp_err_to_name(err));
        s_state = PLAYER_STATE_ERROR;
        return err;
    }
    char uri[PLAYER_URI_MAX];
    err = s_build_uri(path, uri, sizeof(uri));
    if (err != ESP_OK) {
        return err;
    }
    /* 若正在播放，先停止（会关闭 codec，下一次 run 重新打开） */
    if (s_state == PLAYER_STATE_PLAYING || s_state == PLAYER_STATE_PAUSED) {
        (void)esp_audio_simple_player_stop(s_player);
    }
    esp_gmf_err_t gerr = esp_audio_simple_player_run(s_player, uri, NULL);
    if (gerr != ESP_GMF_ERR_OK) {
        ESP_LOGE(TAG, "run %s failed: %d", uri, (int)gerr);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "playing %s", uri);
    return ESP_OK;
}

esp_err_t player_stop(void)
{
    if (s_player == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_gmf_err_t err = esp_audio_simple_player_stop(s_player);
    return (err == ESP_GMF_ERR_OK) ? ESP_OK : ESP_FAIL;
}

esp_err_t player_pause(void)
{
    if (s_player == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_gmf_err_t err = esp_audio_simple_player_pause(s_player);
    return (err == ESP_GMF_ERR_OK) ? ESP_OK : ESP_FAIL;
}

esp_err_t player_resume(void)
{
    if (s_player == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_gmf_err_t err = esp_audio_simple_player_resume(s_player);
    return (err == ESP_GMF_ERR_OK) ? ESP_OK : ESP_FAIL;
}

esp_err_t player_set_volume(uint8_t pct)
{
    if (pct > 100U) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_codec == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    if (esp_codec_dev_set_out_vol(s_codec, (float)pct) != ESP_CODEC_DEV_OK) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t player_get_state(player_state_t *state)
{
    if (state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *state = s_state;
    return ESP_OK;
}

void player_register_event_cb(player_event_cb_t cb)
{
    s_cb = cb;
}

/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file bt_audio.c
 * @brief Bluetooth Classic A2DP sink (docs/PLAN.md 6.4).
 *
 * Flow:
 *   esp_bt_controller (BR/EDR only) -> Bluedroid -> A2DP sink (legacy API:
 *   Bluedroid decodes SBC internally, PCM out) -> esp_codec_dev -> ES8389
 *   -> NS4150B. Sample rate follows the negotiated SBC config
 *   (16/32/44.1/48 kHz, 16 bit, mono/stereo); the shared I2S clock domain
 *   is reconfigured by esp_codec_dev_open().
 *
 * Mutual exclusion with the local player: on AUDIO_STATE STARTED the local
 * player is stopped first (player_stop), so the codec can be reopened at
 * the BT sample rate. On STOPPED/DISCONNECTED the codec is closed and the
 * amplifier is muted, leaving the audio path free for the player again.
 */

#include <string.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
/* 新 API 头：esp_a2d_mcc_t / esp_a2d_cb_event_t / esp_a2d_register_callback /
 * esp_a2d_sink_init；legacy 头：esp_a2d_sink_register_data_callback（PCM 输出，
 * Bluedroid 内部解 SBC）。两者共存，勿用 legacy 头里的旧类型。 */
#include "esp_a2dp_api.h"
#include "esp_a2dp_legacy_api.h"
#include "esp_codec_dev.h"
#include "bsp_codec.h"
#include "bsp_amplifier.h"
#include "player.h"
#include "bt_audio.h"

#define TAG "bt_audio"

/* A2DP SBC 采样率索引（esp_a2d_cie_sbc_t.samp_freq） */
#define BT_SBC_SAMP_16K    0U
#define BT_SBC_SAMP_32K    1U
#define BT_SBC_SAMP_44K1   2U
#define BT_SBC_SAMP_48K    3U

static esp_codec_dev_handle_t s_codec;
static bool s_enabled;
static bool s_connected;
static bool s_streaming;
static volatile bool s_codec_open;   /* 跨任务（BT 栈回调 vs 状态回调） */
static uint32_t s_sample_rate;

static esp_err_t s_open_codec(uint32_t sample_rate)
{
    if (s_codec == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    if (s_codec_open) {
        return ESP_OK;
    }
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = (int)sample_rate,
        .channel = 2,
        .bits_per_sample = 16,
    };
    if (esp_codec_dev_open(s_codec, &fs) != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "codec open %lu Hz failed", (unsigned long)sample_rate);
        return ESP_FAIL;
    }
    s_codec_open = true;
    s_sample_rate = sample_rate;
    (void)bsp_amp_enable(true);
    (void)bsp_amp_mute(false);
    ESP_LOGI(TAG, "codec opened at %lu Hz (2ch/16bit)", (unsigned long)sample_rate);
    return ESP_OK;
}

static void s_close_codec(void)
{
    if (s_codec_open) {
        (void)bsp_amp_mute(true);
        (void)bsp_amp_enable(false);
        (void)esp_codec_dev_close(s_codec);
        s_codec_open = false;
        ESP_LOGI(TAG, "codec closed");
    }
}

/* A2DP PCM 数据回调（Bluedroid 内部已解码 SBC；BT 栈任务上下文）。
 * 注意：esp_codec_dev_write 为阻塞写（I2S DMA），短时阻塞可接受；
 * 若 BT 栈饥饿需改 ring buffer + 独立音频任务（官方 korvo 方案）。
 * s_codec_open 为 volatile，避免 SUSPEND/DISCONNECTED 关闭后写已关设备 */
static void s_pcm_data(const uint8_t *buf, uint32_t len)
{
    if (!s_codec_open || (buf == NULL) || (len == 0U)) {
        return;
    }
    /* esp_codec_dev_write 不修改数据；显式丢弃 const（API 无 const） */
    (void)esp_codec_dev_write(s_codec, (void *)buf, (int)len);
}

static uint32_t s_rate_from_mcc(const esp_a2d_mcc_t *mcc)
{
    if ((mcc == NULL) || (mcc->type != ESP_A2D_MCT_SBC)) {
        return 0U;
    }
    switch (mcc->cie.sbc_info.samp_freq) {
    case BT_SBC_SAMP_16K:
        return 16000U;
    case BT_SBC_SAMP_32K:
        return 32000U;
    case BT_SBC_SAMP_44K1:
        return 44100U;
    case BT_SBC_SAMP_48K:
        return 48000U;
    default:
        return 0U;
    }
}

static void s_a2dp_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param)
{
    switch (event) {
    case ESP_A2D_CONNECTION_STATE_EVT:
        if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
            s_connected = true;
            ESP_LOGI(TAG, "A2DP connected");
        } else if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
            s_connected = false;
            s_streaming = false;
            s_close_codec();
            ESP_LOGI(TAG, "A2DP disconnected");
        }
        break;
    case ESP_A2D_AUDIO_CFG_EVT:
        s_sample_rate = s_rate_from_mcc(&param->audio_cfg.mcc);
        ESP_LOGI(TAG, "A2DP codec cfg: %lu Hz", (unsigned long)s_sample_rate);
        break;
    case ESP_A2D_AUDIO_STATE_EVT:
        if (param->audio_stat.state == ESP_A2D_AUDIO_STATE_STARTED) {
            ESP_LOGI(TAG, "A2DP streaming start");
            if (s_sample_rate == 0U) {
                s_sample_rate = 44100U;   /* SBC 默认 */
            }
            /* 音频焦点：停止本地播放器，独占 codec */
            player_state_t pst = PLAYER_STATE_IDLE;
            if ((player_get_state(&pst) == ESP_OK) &&
                ((pst == PLAYER_STATE_PLAYING) || (pst == PLAYER_STATE_PAUSED))) {
                ESP_LOGI(TAG, "local player active, stopping it");
                (void)player_stop();
            }
            if (s_open_codec(s_sample_rate) == ESP_OK) {
                s_streaming = true;
            } else {
                ESP_LOGE(TAG, "codec unavailable (player busy?); BT audio dropped");
            }
        } else if (param->audio_stat.state == ESP_A2D_AUDIO_STATE_SUSPEND) {
            /* 流暂停/停止（master 枚举仅 SUSPEND/STARTED，无 STOPPED） */
            s_streaming = false;
            s_close_codec();
            ESP_LOGI(TAG, "A2DP stream suspended");
        }
        break;
    default:
        break;
    }
}

esp_err_t bt_audio_get_state(bt_audio_state_t *state)
{
    if (state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    state->enabled = s_enabled;
    state->connected = s_connected;
    state->streaming = s_streaming;
    state->codec_open = s_codec_open;
    state->sample_rate = s_sample_rate;
    return ESP_OK;
}

esp_err_t bt_audio_init(void)
{
    if (s_enabled) {
        return ESP_OK;
    }
    if (s_codec == NULL) {
        s_codec = bsp_codec_get_handle();
        if (s_codec == NULL) {
            ESP_LOGE(TAG, "codec not ready (bsp_codec init failed?)");
            return ESP_ERR_NOT_FOUND;
        }
    }

    /* 只用 Classic：释放 BLE 控制器内存 */
    (void)esp_bt_controller_mem_release(ESP_BT_MODE_BLE);

    esp_bt_controller_config_t ctl_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_bt_controller_init(&ctl_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "controller init failed: %s", esp_err_to_name(err));
        return err;
    }
    err = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "controller enable failed: %s", esp_err_to_name(err));
        return err;
    }
    err = esp_bluedroid_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bluedroid init failed: %s", esp_err_to_name(err));
        return err;
    }
    err = esp_bluedroid_enable();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bluedroid enable failed: %s", esp_err_to_name(err));
        return err;
    }

    /* A2DP sink（legacy API：Bluedroid 内部解 SBC，输出 PCM） */
    err = esp_a2d_register_callback(s_a2dp_cb);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "a2dp register callback failed: %s", esp_err_to_name(err));
        return err;
    }
    err = esp_a2d_sink_register_data_callback(s_pcm_data);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "a2dp sink data callback failed: %s", esp_err_to_name(err));
        return err;
    }
    err = esp_a2d_sink_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "a2dp sink init failed: %s", esp_err_to_name(err));
        return err;
    }

    /* 可发现 + 可连接 */
    err = esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gap set scan mode failed: %s", esp_err_to_name(err));
        return err;
    }

    s_enabled = true;
    ESP_LOGI(TAG, "BT A2DP sink ready (discoverable as \"ESP32\")");
    return ESP_OK;
}

/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BT A2DP Sink wrapper — uses official ESP-IDF example utilities
 */

#include "bt_audio.h"

#include <string.h>
#include "esp_a2dp_api.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_log.h"

#include "bt_app_core.h"
#include "bredr_app_common_utils.h"
#include "a2dp_sink_common_utils.h"
#include "a2dp_sink_int_codec_utils.h"
#include "a2dp_utils_tags.h"

static const char *TAG = "bt_audio";
static volatile bool s_connected;

#define LOCAL_DEVICE_NAME "ESP32-S31-Speaker"

enum { BT_APP_EVT_STACK_UP = 0 };

/* ---- BT stack event handler ---- */
static void bt_av_hdl_stack_evt(uint16_t event, void *param)
{
    (void)param;
    if (event != BT_APP_EVT_STACK_UP) return;

    esp_bt_dev_set_device_name(LOCAL_DEVICE_NAME);
    esp_bt_gap_register_callback(bredr_app_gap_evt_def_hdl);

    esp_a2d_register_callback(bt_a2d_evt_int_codec_hdl);
    esp_a2d_sink_init();
    esp_a2d_sink_register_data_callback(bt_a2d_data_hdl);

    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
    ESP_LOGI(TAG, "BT A2DP sink ready");
}

/* ---- Public API ---- */
esp_err_t bt_audio_init(void)
{
    ESP_ERROR_CHECK(bredr_app_common_init());
    bt_app_task_start_up();
    bt_app_work_dispatch(bt_av_hdl_stack_evt, BT_APP_EVT_STACK_UP, NULL, 0, NULL, NULL);
    return ESP_OK;
}

bool bt_audio_is_connected(void)
{
    return s_connected;
}

const char *bt_audio_device_name(void)
{
    return bt_a2d_get_peer_dev_name();
}

/* Let the A2DP common utils update our connection flag */
void bt_audio_notify_connected(bool connected)
{
    s_connected = connected;
}

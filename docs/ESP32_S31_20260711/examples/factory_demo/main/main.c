/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "bsp/esp32_s31_korvo.h"
#include "app_ui.h"
#include "esp_check.h"
#include "esp_log.h"
#include "lvgl.h"
#include "net_wifi.h"

extern void app_ui_create(void);

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "Starting weather clock demo");

    bsp_display_config_t display_cfg = BSP_DISPLAY_DEFAULT_CONFIG();
    display_cfg.tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_DOUBLE_FULL;

    lv_display_t *disp = bsp_display_start_with_config(&display_cfg);
    if (!disp) {
        ESP_LOGE(TAG, "Display start failed");
        return;
    }
    ESP_ERROR_CHECK(net_wifi_init());

    char ssid[NET_WIFI_SSID_MAX_LEN] = {0};
    char pass[64] = {0};
    if (net_wifi_load_creds(ssid, sizeof(ssid), pass, sizeof(pass))) {
        net_wifi_connect(ssid, pass, false);
    }

    if (bsp_display_lock(-1)) {
        lv_obj_t *scr = lv_disp_get_scr_act(disp);
        app_ui_create();
        bsp_display_unlock();
    } else {
        ESP_LOGE(TAG, "LVGL lock failed");
    }
}

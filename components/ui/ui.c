/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file ui.c
 * @brief LVGL bring-up via esp_lvgl_adapter (official S31 scheme).
 *
 * Flow (mirrors espressif/esp-dev-kits esp32-s31-korvo BSP):
 *   1. bsp_display panel (RGB, 720x720, RGB565, 2x PSRAM frame buffers)
 *   2. esp_lv_adapter_init / register_display (TEAR_AVOID_MODE_DOUBLE_FULL:
 *      the two panel frame buffers are used directly as draw buffers)
 *   3. register FT6336U touch (esp_lcd_touch_ft5x06) when available
 *   4. esp_lv_adapter_start
 *   5. run the LVGL official benchmark demo (FPS) on boot
 *
 * See docs/PLAN.md 2.4.2e.
 */

#include "sdkconfig.h"
#include "esp_log.h"
#include "lvgl.h"
#include "esp_lv_adapter.h"
#include "bsp_display.h"
#include "bsp_touch.h"
#include "ui.h"

#if LV_USE_DEMO_BENCHMARK
#include "demos/lv_demos.h"
#endif

#define TAG "ui"

static lv_display_t *s_display;

esp_err_t ui_init(void)
{
    /* 1. RGB panel（BSP 已在 bsp_init 中创建） */
    esp_lcd_panel_handle_t panel = NULL;
    esp_err_t err = bsp_display_get_handle(&panel);
    if ((err != ESP_OK) || (panel == NULL)) {
        ESP_LOGE(TAG, "display not ready (bsp_display init failed?)");
        return ESP_ERR_INVALID_STATE;
    }
    uint16_t h_res = 0U;
    uint16_t v_res = 0U;
    (void)bsp_display_get_resolution(&h_res, &v_res);
    ESP_LOGI(TAG, "panel %ux%u", (unsigned)h_res, (unsigned)v_res);

    /* 2. LVGL adapter 初始化 */
    if (!esp_lv_adapter_is_initialized()) {
        esp_lv_adapter_config_t adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG();
        adapter_cfg.task_stack_size = CONFIG_LERO_UI_TASK_STACK;
        adapter_cfg.task_priority = CONFIG_LERO_UI_TASK_PRIORITY;
        adapter_cfg.task_core_id = CONFIG_LERO_UI_TASK_CORE;
        err = esp_lv_adapter_init(&adapter_cfg);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "adapter init failed: %s", esp_err_to_name(err));
            return err;
        }
    }

    /* 3. 注册显示：DOUBLE_FULL —— 直接用 panel 的 2 个 PSRAM 帧缓冲
     *    （无需额外分配，见 PLAN 2.4.2e） */
    esp_lv_adapter_display_config_t display_cfg = ESP_LV_ADAPTER_DISPLAY_RGB_DEFAULT_CONFIG(
        panel, NULL, h_res, v_res, ESP_LV_ADAPTER_ROTATE_0);
    display_cfg.tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_DOUBLE_FULL;
    s_display = esp_lv_adapter_register_display(&display_cfg);
    if (s_display == NULL) {
        ESP_LOGE(TAG, "register display failed");
        return ESP_FAIL;
    }

    /* 4. 触摸（FT6336U；失败仅降级，不影响显示） */
    if (bsp_touch_get_handle() != NULL) {
        esp_lv_adapter_touch_config_t touch_cfg =
            ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(s_display, bsp_touch_get_handle());
        if (esp_lv_adapter_register_touch(&touch_cfg) == NULL) {
            ESP_LOGW(TAG, "register touch failed; continue without touch");
        }
    } else {
        ESP_LOGW(TAG, "touch not ready; continue without touch");
    }

    /* 5. 启动 LVGL 任务 */
    err = esp_lv_adapter_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adapter start failed: %s", esp_err_to_name(err));
        return err;
    }

    /* 6. 背光已在 bsp_display_init 点亮（上电即亮）；此处保持 100% 兜底，
     *    再启动官方 benchmark demo（显示 FPS） */
    (void)bsp_display_backlight_set(100U);
#if CONFIG_LERO_UI_DEMO_BENCHMARK
    if (esp_lv_adapter_lock(-1) == ESP_OK) {
        lv_demo_benchmark();
        esp_lv_adapter_unlock();
        ESP_LOGI(TAG, "LVGL benchmark demo started (FPS top-right)");
    }
#endif
    ESP_LOGI(TAG, "ui ready (%ux%u @ RGB565)", (unsigned)h_res, (unsigned)v_res);
    return ESP_OK;
}

lv_display_t *ui_get_display(void)
{
    return s_display;
}

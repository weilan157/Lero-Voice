/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file bsp_display.c
 * @brief RGB LCD panel + LEDC backlight.
 */

#include <string.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "bsp_config.h"
#include "bsp_display.h"

#define TAG "bsp_display"

#define LCD_COLOR_BYTES         3U            /* RGB888 */
#define LCD_FB_BYTES            ((uint32_t)CONFIG_LERO_LCD_H_RES * CONFIG_LERO_LCD_V_RES * LCD_COLOR_BYTES)
#define BL_DUTY_MAX             255U

EXT_RAM_BSS_ATTR static uint8_t s_fb0[LCD_FB_BYTES];
EXT_RAM_BSS_ATTR static uint8_t s_fb1[LCD_FB_BYTES];

static esp_lcd_panel_handle_t s_panel;
static bool s_initialized;

static void s_fill_data_gpios(gpio_num_t data[ESP_LCD_RGB_BUS_WIDTH_MAX])
{
    data[0] = BSP_LCD_DB0_GPIO;
    data[1] = BSP_LCD_DB1_GPIO;
    data[2] = BSP_LCD_DB2_GPIO;
    data[3] = BSP_LCD_DB3_GPIO;
    data[4] = BSP_LCD_DB4_GPIO;
    data[5] = BSP_LCD_DB5_GPIO;
    data[6] = BSP_LCD_DB6_GPIO;
    data[7] = BSP_LCD_DB7_GPIO;
    data[8] = BSP_LCD_DB8_GPIO;
    data[9] = BSP_LCD_DB9_GPIO;
    data[10] = BSP_LCD_DB10_GPIO;
    data[11] = BSP_LCD_DB11_GPIO;
    data[12] = BSP_LCD_DB12_GPIO;
    data[13] = BSP_LCD_DB13_GPIO;
    data[14] = BSP_LCD_DB14_GPIO;
    data[15] = BSP_LCD_DB15_GPIO;
    data[16] = BSP_LCD_DB16_GPIO;
    data[17] = BSP_LCD_DB17_GPIO;
}

static esp_err_t s_reset_panel_gpio(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << (uint64_t)BSP_LCD_RESET_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        return err;
    }
    (void)gpio_set_level(BSP_LCD_RESET_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(20U));
    (void)gpio_set_level(BSP_LCD_RESET_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(20U));
    return ESP_OK;
}

static esp_err_t s_backlight_init(void)
{
    ledc_timer_config_t timer_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = CONFIG_LERO_LCD_BL_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer_cfg);
    if (err != ESP_OK) {
        return err;
    }
    ledc_channel_config_t ch_cfg = {
        .gpio_num = BSP_LCD_BL_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
    };
    return ledc_channel_config(&ch_cfg);
}

esp_err_t bsp_display_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    esp_err_t err = s_backlight_init();          /* 背光默认关闭（PLAN 3.3.1 #2） */
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "backlight init failed: %s", esp_err_to_name(err));
        return err;
    }
    err = s_reset_panel_gpio();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "reset gpio failed: %s", esp_err_to_name(err));
        return err;
    }

    esp_lcd_rgb_panel_config_t cfg = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz = CONFIG_LERO_LCD_PIXEL_CLOCK_HZ,
            .h_res = CONFIG_LERO_LCD_H_RES,
            .v_res = CONFIG_LERO_LCD_V_RES,
            .hsync_pulse_width = CONFIG_LERO_LCD_HSYNC_PULSE,
            .hsync_back_porch = CONFIG_LERO_LCD_HBP,
            .hsync_front_porch = CONFIG_LERO_LCD_HFP,
            .vsync_pulse_width = CONFIG_LERO_LCD_VSYNC_PULSE,
            .vsync_back_porch = CONFIG_LERO_LCD_VBP,
            .vsync_front_porch = CONFIG_LERO_LCD_VFP,
            .flags.pclk_active_neg = true,
        },
        .data_width = 18,
        .in_color_format = LCD_COLOR_FMT_RGB888,
        .out_color_format = LCD_COLOR_FMT_RGB666,   /* 18-bit 并口对应 RGB666 */
        .num_fbs = 2,
        .user_fbs = { s_fb0, s_fb1 },
        .bounce_buffer_size_px = 0,
        .dma_burst_size = 64,
        .hsync_gpio_num = BSP_LCD_HS_GPIO,
        .vsync_gpio_num = BSP_LCD_VS_GPIO,
        .de_gpio_num = BSP_LCD_DE_GPIO,
        .pclk_gpio_num = BSP_LCD_PCLK_GPIO,
        .disp_gpio_num = -1,
        .flags = { 0 },
    };
    s_fill_data_gpios(cfg.data_gpio_nums);

    err = esp_lcd_new_rgb_panel(&cfg, &s_panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rgb panel create failed: %s", esp_err_to_name(err));
        return err;
    }
    (void)esp_lcd_panel_reset(s_panel);
    err = esp_lcd_panel_init(s_panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rgb panel init failed: %s", esp_err_to_name(err));
        return err;
    }
    (void)esp_lcd_panel_disp_on_off(s_panel, true);

    s_initialized = true;
    ESP_LOGI(TAG, "display %ux%u ready (backlight off)",
             CONFIG_LERO_LCD_H_RES, CONFIG_LERO_LCD_V_RES);
    return ESP_OK;
}

esp_err_t bsp_display_get_handle(esp_lcd_panel_handle_t *panel)
{
    if (panel == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    *panel = s_panel;
    return ESP_OK;
}

esp_err_t bsp_display_get_framebuffers(void **fb0, void **fb1)
{
    if ((fb0 == NULL) || (fb1 == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    *fb0 = (void *)s_fb0;
    *fb1 = (void *)s_fb1;
    return ESP_OK;
}

esp_err_t bsp_display_get_resolution(uint16_t *width, uint16_t *height)
{
    if ((width == NULL) || (height == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    *width = (uint16_t)CONFIG_LERO_LCD_H_RES;
    *height = (uint16_t)CONFIG_LERO_LCD_V_RES;
    return ESP_OK;
}

esp_err_t bsp_display_backlight_set(uint8_t pct)
{
    if (pct > 100U) {
        return ESP_ERR_INVALID_ARG;
    }
    const uint32_t duty = ((uint32_t)pct * BL_DUTY_MAX) / 100U;
    esp_err_t err = ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, (uint32_t)duty);
    if (err == ESP_OK) {
        err = ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    }
    return err;
}


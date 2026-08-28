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

/* S31 esp_lcd RGB 面板不支持 18-bit（运行时报 unsupported data width 18），
 * 18 线面板以 16-bit RGB565 驱动（高 5/6 位有效，颜色略有损失，MVP 可接受；
 * 若要完整 18-bit 色深需 24 线面板或后续驱动支持）。 */
#define LCD_COLOR_BYTES         2U            /* RGB565 */
#define LCD_FB_BYTES            ((uint32_t)CONFIG_LERO_LCD_H_RES * CONFIG_LERO_LCD_V_RES * LCD_COLOR_BYTES)
#define BL_DUTY_MAX             255U

EXT_RAM_BSS_ATTR static uint8_t s_fb0[LCD_FB_BYTES];
EXT_RAM_BSS_ATTR static uint8_t s_fb1[LCD_FB_BYTES];

static esp_lcd_panel_handle_t s_panel;
static bool s_initialized;

static void s_fill_data_gpios(gpio_num_t data[ESP_LCD_RGB_BUS_WIDTH_MAX])
{
    /* 18-bit RGB666 面板（DB0~5=B、DB6~11=G、DB12~17=R，PLAN 2.4.2d）用
     * 16-bit RGB565 直驱时的位对齐：RGB565 各通道**高位对齐**面板对应
     * 通道高位（低位缺失，DB0/DB12 悬空拉低）——
     * 否则 G0 会落进 DB5(B5)、R0 落进 DB11(G5) 造成颜色通道串位。
     * RGB565 位序（IDF RGB panel 16-bit）：data[0..4]=B4..B0,
     * data[5..10]=G5..G0, data[11..15]=R4..R0。
     * 若模组 OTP 实为 16-bit RGB 接口（IC 内重映射），需改回直连映射；
     * 上板用纯色测试条验证（见 PLAN 2.4.2d）。 */
    data[0] = BSP_LCD_DB1_GPIO;     /* B0 -> DB1  */
    data[1] = BSP_LCD_DB2_GPIO;     /* B1 -> DB2  */
    data[2] = BSP_LCD_DB3_GPIO;     /* B2 -> DB3  */
    data[3] = BSP_LCD_DB4_GPIO;     /* B3 -> DB4  */
    data[4] = BSP_LCD_DB5_GPIO;     /* B4 -> DB5  */
    data[5] = BSP_LCD_DB6_GPIO;     /* G0 -> DB6  */
    data[6] = BSP_LCD_DB7_GPIO;     /* G1 -> DB7  */
    data[7] = BSP_LCD_DB8_GPIO;     /* G2 -> DB8  */
    data[8] = BSP_LCD_DB9_GPIO;     /* G3 -> DB9  */
    data[9] = BSP_LCD_DB10_GPIO;    /* G4 -> DB10 */
    data[10] = BSP_LCD_DB11_GPIO;   /* G5 -> DB11 */
    data[11] = BSP_LCD_DB13_GPIO;   /* R0 -> DB13 */
    data[12] = BSP_LCD_DB14_GPIO;   /* R1 -> DB14 */
    data[13] = BSP_LCD_DB15_GPIO;   /* R2 -> DB15 */
    data[14] = BSP_LCD_DB16_GPIO;   /* R3 -> DB16 */
    data[15] = BSP_LCD_DB17_GPIO;   /* R4 -> DB17 */

    /* DB0(IO7)/DB12(IO19) 悬空：拉低避免浮空 */
    const gpio_num_t unused[] = { BSP_LCD_DB0_GPIO, BSP_LCD_DB12_GPIO };
    for (size_t i = 0U; i < (sizeof(unused) / sizeof(unused[0])); i++) {
        gpio_config_t lo = {
            .pin_bit_mask = (1ULL << (uint64_t)unused[i]),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        if (gpio_config(&lo) == ESP_OK) {
            (void)gpio_set_level(unused[i], 0);
        }
    }
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

    esp_err_t err = s_backlight_init();          /* LEDC 通道就绪（duty=0） */
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
        .data_width = 16,
        .in_color_format = LCD_COLOR_FMT_RGB565,
        .out_color_format = LCD_COLOR_FMT_RGB565,   /* S31 不支持 18-bit */
        .num_fbs = 2,
        .user_fbs = { s_fb0, s_fb1 },
        .bounce_buffer_size_px = 0,
        .dma_burst_size = 128,
        .hsync_gpio_num = BSP_LCD_HS_GPIO,
        .vsync_gpio_num = BSP_LCD_VS_GPIO,
        .de_gpio_num = BSP_LCD_DE_GPIO,
        .pclk_gpio_num = BSP_LCD_PCLK_GPIO,
        .disp_gpio_num = -1,
        /* fb 在 PSRAM（EXT_RAM_BSS_ATTR）：必须告知驱动走 PSRAM 路径
         * （官方 korvo BSP 同 S31 芯片亦设 fb_in_psram=true），否则
         * DMA 按内部 SRAM fb 处理，屏幕无输出（背光亮但全黑） */
        .flags = {
            .fb_in_psram = true,
        },
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

    /* 上电即点亮背光（不依赖 ui_init/LVGL）：屏幕背光使能（BL_EN=IO54，
     * MP3302 高有效 PWM 调光）。用户要求上电使能；LVGL 未就绪时屏幕为
     * 黑屏（fb 全 0），便于确认背光电路与 panel 输出。 */
    (void)bsp_display_backlight_set(100U);

    s_initialized = true;
    ESP_LOGI(TAG, "display %ux%u ready (backlight on)",
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


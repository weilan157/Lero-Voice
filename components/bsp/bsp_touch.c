/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file bsp_touch.c
 * @brief Capacitive touch controller FT6336U (I2C1: SDA=IO46 / SCL=IO47).
 *
 * FT6336U belongs to the FT5x06 protocol family (single/two point),
 * driven via espressif/esp_lcd_touch_ft5x06 + esp_lcd_touch (managed
 * components). INT=IO2 (active low, falling edge), RST=IO48 (active low).
 * See docs/PLAN.md 2.4.3 / 2.4.2d.
 *
 * NOTE (上板实测 2026-08-28): 本模组触摸 I2C 地址为 **7-bit 0x48**
 * （官方源码 8-bit 写地址 0x90 >> 1），并非 FT5x06 常见的 0x38。
 * 组件宏 ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG() 默认 0x38，必须显式覆盖
 * io_cfg.dev_addr，否则读 ID 失败、触摸全程无响应。
 */

#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_lcd_touch_ft5x06.h"
#include "bsp_config.h"
#include "bsp_i2c.h"
#include "bsp_touch.h"

#define TAG "bsp_touch"

#define TOUCH_I2C_ADDR      0x48U   /* FT6336U 本模组变体：7-bit 0x48
                                     * （官方源码 8-bit 写 0x90 >> 1）；
                                     * 非 FT5x06 常见的 0x38 */

static esp_lcd_touch_handle_t s_touch;

static esp_err_t s_gpio_init(void)
{
    gpio_config_t int_cfg = {
        .pin_bit_mask = (1ULL << (uint64_t)BSP_TOUCH_INT_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&int_cfg);
    if (err != ESP_OK) {
        return err;
    }
    gpio_config_t rst_cfg = {
        .pin_bit_mask = (1ULL << (uint64_t)BSP_TOUCH_RST_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    err = gpio_config(&rst_cfg);
    if (err != ESP_OK) {
        return err;
    }
    /* 复位脉冲：低≥1ms → 高；FT6x36 手册要求复位释放后 **≥300 ms**
     * 才能进行 I2C 通信（否则读 ID 失败 → 触摸无响应）。此前仅等 5ms
     * 导致芯片未就绪即创建驱动（上板实测：触摸没反应）。 */
    (void)gpio_set_level(BSP_TOUCH_RST_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(2U));
    (void)gpio_set_level(BSP_TOUCH_RST_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(350U));
    return ESP_OK;
}

esp_err_t bsp_touch_init(void)
{
    if (s_touch != NULL) {
        return ESP_OK;
    }
    esp_err_t err = s_gpio_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "touch gpio init failed: %s", esp_err_to_name(err));
        return err;
    }
    i2c_master_bus_handle_t bus = NULL;
    err = bsp_i2c_get_bus1(&bus);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "I2C1 unavailable (%s); touch disabled", esp_err_to_name(err));
        return err;
    }

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG();
    io_cfg.scl_speed_hz = BSP_I2C1_FREQ_HZ;
    io_cfg.dev_addr = TOUCH_I2C_ADDR;   /* 覆盖宏默认 0x38 → 0x48（上板实测） */
    err = esp_lcd_new_panel_io_i2c(bus, &io_cfg, &io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "touch panel io failed: %s", esp_err_to_name(err));
        return err;
    }

    esp_lcd_touch_config_t touch_cfg = {
        .x_max = CONFIG_LERO_LCD_H_RES,
        .y_max = CONFIG_LERO_LCD_V_RES,
        .rst_gpio_num = BSP_TOUCH_RST_GPIO,
        .int_gpio_num = BSP_TOUCH_INT_GPIO,
        .levels = {
            .reset = 0,         /* 低电平复位 */
            .interrupt = 0,     /* 低电平中断（下降沿） */
        },
        .flags = { 0 },         /* rotation 0：无 swap/mirror */
    };
    err = esp_lcd_touch_new_i2c_ft5x06(io, &touch_cfg, &s_touch);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ft5x06(FT6336U) create failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "touch ready: FT6336U @0x%02X (%ux%u)",
             (unsigned)TOUCH_I2C_ADDR,
             (unsigned)CONFIG_LERO_LCD_H_RES, (unsigned)CONFIG_LERO_LCD_V_RES);
    return ESP_OK;
}

esp_lcd_touch_handle_t bsp_touch_get_handle(void)
{
    return s_touch;
}

esp_err_t bsp_touch_read_point(bsp_touch_point_t *point)
{
    if (point == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_touch == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    /* 新版 esp_lcd_touch API：先读控制器，再取坐标（旧 get_coordinates 已弃用） */
    esp_err_t err = esp_lcd_touch_read_data(s_touch);
    if (err != ESP_OK) {
        return err;
    }
    esp_lcd_touch_point_data_t data = {0U};
    uint8_t count = 0U;
    err = esp_lcd_touch_get_data(s_touch, &data, &count, 1U);
    if (err != ESP_OK) {
        return err;
    }
    if (count == 0U) {
        point->pressed = false;
        point->x = 0U;
        point->y = 0U;
    } else {
        point->pressed = true;
        point->x = data.x;
        point->y = data.y;
    }
    return ESP_OK;
}

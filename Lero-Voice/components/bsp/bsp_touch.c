/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file bsp_touch.c
 * @brief Touch controller scan + placeholder point API.
 */

#include "esp_log.h"
#include "driver/gpio.h"
#include "bsp_config.h"
#include "bsp_i2c.h"
#include "bsp_touch.h"

#define TAG "bsp_touch"

static const uint16_t s_candidate_addrs[] = {
    0x15U,  /* CST816 */
    0x38U,  /* FT6236 */
    0x48U,  /* NS2009 / TSC2007 */
    0x5AU,  /* GT911 (variant) */
    0x5DU,  /* GT911 */
    0x14U,  /* FT5x06 (variant) */
};
#define TOUCH_CANDIDATE_COUNT  (sizeof(s_candidate_addrs) / sizeof(s_candidate_addrs[0]))

static bool s_found;
static uint16_t s_found_addr;

esp_err_t bsp_touch_init(void)
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
    (void)gpio_set_level(BSP_TOUCH_RST_GPIO, 1);

    for (size_t i = 0U; i < TOUCH_CANDIDATE_COUNT; i++) {
        i2c_master_dev_handle_t dev = NULL;
        err = bsp_i2c_add_device1(s_candidate_addrs[i], BSP_I2C1_FREQ_HZ, &dev);
        if (err != ESP_OK) {
            continue;
        }
        uint8_t probe = 0x00U;
        err = i2c_master_transmit_receive(dev, &probe, 1U, &probe, 1U, 50);
        if (err == ESP_OK) {
            s_found = true;
            s_found_addr = s_candidate_addrs[i];
            ESP_LOGI(TAG, "touch candidate ACK at 0x%02X (panel model TBD, PLAN 11 #1)",
                     (unsigned)s_candidate_addrs[i]);
        }
    }

    if (!s_found) {
        ESP_LOGW(TAG, "no touch controller answered on I2C1");
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "touch ready (addr 0x%02X, read API pending panel id)",
             (unsigned)s_found_addr);
    return ESP_OK;
}

esp_err_t bsp_touch_read_point(bsp_touch_point_t *point)
{
    if (point == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_found) {
        return ESP_ERR_NOT_FOUND;
    }
    /* TODO(M6): controller specific read after the panel is identified;
     * integrate esp_lcd_touch (managed component) here. */
    return ESP_ERR_NOT_SUPPORTED;
}


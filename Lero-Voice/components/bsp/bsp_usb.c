/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file bsp_usb.c
 * @brief USB load switch control.
 */

#include "esp_log.h"
#include "driver/gpio.h"
#include "bsp_config.h"
#include "bsp_usb.h"

#define TAG "bsp_usb"

esp_err_t bsp_usb_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << (uint64_t)BSP_USB_EN_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&cfg);
    if (err == ESP_OK) {
        err = gpio_set_level(BSP_USB_EN_GPIO, 0);
    }
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "usb switch ready (off by default)");
    }
    return err;
}

esp_err_t bsp_usb_enable(bool on)
{
    esp_err_t err = gpio_set_level(BSP_USB_EN_GPIO, on ? 1 : 0);
    ESP_LOGD(TAG, "usb %s", on ? "on" : "off");
    return err;
}


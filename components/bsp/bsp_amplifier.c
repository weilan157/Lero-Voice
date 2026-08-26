/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file bsp_amplifier.c
 * @brief NS4150B amplifier enable / mute.
 */

#include "esp_log.h"
#include "driver/gpio.h"
#include "bsp_config.h"
#include "bsp_amplifier.h"

#define TAG "bsp_amp"

static bool s_enabled;
static bool s_muted;

static esp_err_t s_apply(void)
{
    const bool pa_on = s_enabled && !s_muted;
    return gpio_set_level(BSP_PA_CTRL_GPIO, pa_on ? 1 : 0);
}

esp_err_t bsp_amp_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << (uint64_t)BSP_PA_CTRL_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&cfg);
    if (err == ESP_OK) {
        s_enabled = false;
        s_muted = true;             /* 上电默认静音，防爆音（PLAN 3.3.1 #3） */
        err = s_apply();
    }
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "amplifier ready (muted)");
    }
    return err;
}

esp_err_t bsp_amp_enable(bool on)
{
    s_enabled = on;
    esp_err_t err = s_apply();
    ESP_LOGD(TAG, "enable=%d mute=%d -> pa %s", (int)s_enabled, (int)s_muted,
             (s_enabled && !s_muted) ? "on" : "off");
    return err;
}

esp_err_t bsp_amp_mute(bool mute)
{
    s_muted = mute;
    esp_err_t err = s_apply();
    ESP_LOGD(TAG, "mute=%d", (int)s_muted);
    return err;
}


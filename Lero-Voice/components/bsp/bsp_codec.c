/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file bsp_codec.c
 * @brief ES8389 probe + interface placeholder.
 *
 * TODO(M7): replace the body of set_volume/mute with esp_codec_dev
 * (espressif/esp_codec_dev >= v1.3.6, managed component). The public BSP
 * interface below must not change.
 */

#include "esp_log.h"
#include "bsp_config.h"
#include "bsp_i2c.h"
#include "bsp_codec.h"

#define TAG "bsp_codec"

static i2c_master_dev_handle_t s_dev;
static bool s_present;

esp_err_t bsp_codec_init(void)
{
    i2c_master_dev_handle_t dev = NULL;
    esp_err_t err = bsp_i2c_add_device0(BSP_ES8389_I2C_ADDR, BSP_I2C0_FREQ_HZ, &dev);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "add device failed: %s", esp_err_to_name(err));
        return err;
    }
    uint8_t reg = 0x00U;
    uint8_t val = 0U;
    err = i2c_master_transmit_receive(dev, &reg, 1U, &val, 1U, 100);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "codec 0x20 no ACK (%s); MCLK may be missing (PLAN 2.6 #1)",
                 esp_err_to_name(err));
        return err;
    }
    s_dev = dev;
    s_present = true;
    ESP_LOGI(TAG, "ES8389 present at 0x20 (reg0=0x%02X)", (unsigned)val);
    return ESP_OK;
}

bool bsp_codec_is_present(void)
{
    return s_present;
}

esp_err_t bsp_codec_set_volume(uint8_t volume_pct)
{
    if (!s_present) {
        return ESP_ERR_NOT_FOUND;
    }
    if (volume_pct > 100U) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGD(TAG, "set volume %u%% (esp_codec_dev integration pending, PLAN 6)",
             (unsigned)volume_pct);
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t bsp_codec_mute(bool mute)
{
    if (!s_present) {
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGD(TAG, "mute %d (esp_codec_dev integration pending, PLAN 6)", (int)mute);
    return ESP_ERR_NOT_SUPPORTED;
}


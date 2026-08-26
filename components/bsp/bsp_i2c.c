/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file bsp_i2c.c
 * @brief I2C master bus ownership (internal to BSP).
 */

#include <string.h>
#include "esp_log.h"
#include "bsp_config.h"
#include "bsp_i2c.h"

#define TAG "bsp_i2c"

static i2c_master_bus_handle_t s_bus0;
static i2c_master_bus_handle_t s_bus1;
static bool s_bus0_created;
static bool s_bus1_created;

static esp_err_t s_create_bus(i2c_port_num_t port, gpio_num_t sda, gpio_num_t scl,
                              uint32_t freq_hz, i2c_master_bus_handle_t *out)
{
    i2c_master_bus_config_t cfg = {
        .i2c_port = port,
        .sda_io_num = sda,
        .scl_io_num = scl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags.enable_internal_pullup = true,
    };
    return i2c_new_master_bus(&cfg, out);
}

esp_err_t bsp_i2c_init(void)
{
    esp_err_t err = ESP_OK;
    if (!s_bus0_created) {
        err = s_create_bus(I2C_NUM_0, BSP_I2C0_SDA_GPIO, BSP_I2C0_SCL_GPIO,
                           BSP_I2C0_FREQ_HZ, &s_bus0);
        if (err == ESP_OK) {
            s_bus0_created = true;
        } else {
            ESP_LOGE(TAG, "I2C0 create failed: %s", esp_err_to_name(err));
        }
    }
    if (!s_bus1_created) {
        err = s_create_bus(I2C_NUM_1, BSP_I2C1_SDA_GPIO, BSP_I2C1_SCL_GPIO,
                           BSP_I2C1_FREQ_HZ, &s_bus1);
        if (err == ESP_OK) {
            s_bus1_created = true;
        } else {
            ESP_LOGE(TAG, "I2C1 create failed: %s", esp_err_to_name(err));
        }
    }
    return (s_bus0_created && s_bus1_created) ? ESP_OK : ESP_FAIL;
}

esp_err_t bsp_i2c_deinit(void)
{
    esp_err_t err = ESP_OK;
    if (s_bus1_created) {
        err = i2c_del_master_bus(s_bus1);
        s_bus1_created = (err != ESP_OK);
    }
    if (s_bus0_created) {
        err = i2c_del_master_bus(s_bus0);
        s_bus0_created = (err != ESP_OK);
    }
    return err;
}

static esp_err_t s_add_device(i2c_master_bus_handle_t bus, uint16_t addr,
                              uint32_t speed_hz, i2c_master_dev_handle_t *dev)
{
    if (bus == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = speed_hz,
        .scl_wait_us = 0U,
        .flags.disable_ack_check = false,
    };
    return i2c_master_bus_add_device(bus, &cfg, dev);
}

esp_err_t bsp_i2c_add_device0(uint16_t addr, uint32_t speed_hz, i2c_master_dev_handle_t *dev)
{
    return s_add_device(s_bus0, addr, speed_hz, dev);
}

esp_err_t bsp_i2c_add_device1(uint16_t addr, uint32_t speed_hz, i2c_master_dev_handle_t *dev)
{
    return s_add_device(s_bus1, addr, speed_hz, dev);
}

esp_err_t bsp_i2c_get_bus0(i2c_master_bus_handle_t *bus)
{
    if (bus == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_bus0_created) {
        return ESP_ERR_INVALID_STATE;
    }
    *bus = s_bus0;
    return ESP_OK;
}


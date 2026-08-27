/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file bsp_i2c.h
 * @brief Internal I2C bus management (not part of the public BSP API).
 *
 * I2C0 (IO0/IO1): ES8389 + QMI8658A; I2C1 (IO46/IO47): touch controller.
 */

#ifndef BSP_I2C_H
#define BSP_I2C_H

#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create both I2C master buses (idempotent).
 * @return ESP_OK on success.
 */
esp_err_t bsp_i2c_init(void);

/**
 * @brief Delete both I2C master buses.
 * @return ESP_OK on success.
 */
esp_err_t bsp_i2c_deinit(void);

/**
 * @brief Add a device on I2C0 (codec / IMU bus).
 * @param[in]  addr       7-bit device address.
 * @param[in]  speed_hz   SCL frequency.
 * @param[out] dev        Returned device handle.
 * @return ESP_OK / error code.
 */
esp_err_t bsp_i2c_add_device0(uint16_t addr, uint32_t speed_hz, i2c_master_dev_handle_t *dev);

/**
 * @brief Add a device on I2C1 (touch bus).
 * @param[in]  addr       7-bit device address.
 * @param[in]  speed_hz   SCL frequency.
 * @param[out] dev        Returned device handle.
 * @return ESP_OK / error code.
 */
esp_err_t bsp_i2c_add_device1(uint16_t addr, uint32_t speed_hz, i2c_master_dev_handle_t *dev);

/**
 * @brief Get the I2C0 master bus handle (for esp_codec_dev control interface).
 * @param[out] bus        Returned bus handle.
 * @return ESP_OK / ESP_ERR_INVALID_STATE when the bus is not created.
 */
esp_err_t bsp_i2c_get_bus0(i2c_master_bus_handle_t *bus);

/**
 * @brief Get the I2C1 master bus handle (touch bus, for esp_lcd_touch).
 * @param[out] bus        Returned bus handle.
 * @return ESP_OK / ESP_ERR_INVALID_STATE when the bus is not created.
 */
esp_err_t bsp_i2c_get_bus1(i2c_master_bus_handle_t *bus);

#ifdef __cplusplus
}
#endif

#endif /* BSP_I2C_H */


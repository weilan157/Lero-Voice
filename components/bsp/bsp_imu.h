/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file bsp_imu.h
 * @brief QMI8658A 6-axis IMU (I2C0, polling mode).
 *
 * INT1/INT2 are not wired (docs/PLAN.md 2.6 #6) and SA0 floats, so the
 * driver probes 0x6A / 0x68 first (PLAN 2.6 #7). Values are returned in
 * milli-g (accel) / milli-dps (gyro).
 */

#ifndef BSP_IMU_H
#define BSP_IMU_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int32_t x;  /*< accel: milli-g; gyro: milli-dps */
    int32_t y;
    int32_t z;
} bsp_imu_vec3_t;

/**
 * @brief Probe and initialize the IMU (address auto-detect 0x6A/0x68).
 * @return ESP_OK when the sensor is found and configured.
 */
esp_err_t bsp_imu_init(void);

/**
 * @brief Read raw accelerometer (converted to milli-g).
 * @param[out] accel Filled vector.
 * @return ESP_OK / ESP_ERR_NOT_FOUND when no sensor.
 */
esp_err_t bsp_imu_read_accel(bsp_imu_vec3_t *accel);

/**
 * @brief Read raw gyroscope (converted to milli-dps).
 * @param[out] gyro Filled vector.
 * @return ESP_OK / ESP_ERR_NOT_FOUND when no sensor.
 */
esp_err_t bsp_imu_read_gyro(bsp_imu_vec3_t *gyro);

/**
 * @brief Read the on-die temperature.
 * @param[out] temp_milli_c Temperature in milli-Celsius (raw × 10;
 *                          absolute accuracy needs calibration).
 * @return ESP_OK / ESP_ERR_NOT_FOUND when no sensor.
 */
esp_err_t bsp_imu_read_temp(int32_t *temp_milli_c);

/**
 * @brief Check whether the IMU was found during init.
 * @return true when present.
 */
bool bsp_imu_is_present(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_IMU_H */


/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file bsp_imu.c
 * @brief QMI8658A minimal polling driver (register map per QMI8658A
 *        datasheet; verify with the 'diag imu' command on first bring-up).
 */

#include <string.h>
#include "esp_log.h"
#include "bsp_config.h"
#include "bsp_i2c.h"
#include "bsp_imu.h"

#define TAG "bsp_imu"

#define QMI8658_REG_WHO_AM_I    0x00U
#define QMI8658_WHO_AM_I_VALUE  0x05U
#define QMI8658_REG_CTRL2       0x03U    /* accel range aFS[5:4]: 01 = +/-4g */
#define QMI8658_REG_CTRL6       0x07U    /* sensor enable: bit0 accel, bit1 gyro */
#define QMI8658_REG_ACC_X_L     0x35U
#define QMI8658_REG_GYR_X_L     0x39U
#define QMI8658_CTRL6_ACC_GYR   0x03U
#define QMI8658_AXIS_BYTES      6U
#define ACCEL_LSB_PER_G         8192     /* +/-4g, 16-bit */
#define GYRO_LSB_PER_DPS        16       /* assume +/-2048dps (verify) */

static i2c_master_dev_handle_t s_dev;
static bool s_present;

static esp_err_t s_read_reg(uint8_t reg, uint8_t *buf, size_t len)
{
    if (s_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_master_transmit_receive(s_dev, &reg, 1U, buf, len, 100);
}

static esp_err_t s_write_reg(uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = { reg, value };
    if (s_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_master_transmit(s_dev, buf, sizeof(buf), 100);
}

esp_err_t bsp_imu_init(void)
{
    uint16_t addrs[2] = { BSP_QMI8658_ADDR_A, BSP_QMI8658_ADDR_B };
    esp_err_t err = ESP_ERR_NOT_FOUND;

    for (size_t i = 0U; i < 2U; i++) {
        i2c_master_dev_handle_t dev = NULL;
        err = bsp_i2c_add_device0(addrs[i], BSP_I2C0_FREQ_HZ, &dev);
        if (err != ESP_OK) {
            continue;
        }
        uint8_t who = 0U;
        err = i2c_master_transmit_receive(dev, (const uint8_t[]){ QMI8658_REG_WHO_AM_I }, 1U, &who, 1U, 100);
        if ((err == ESP_OK) && (who == QMI8658_WHO_AM_I_VALUE)) {
            s_dev = dev;
            s_present = true;
            ESP_LOGI(TAG, "QMI8658A found at 0x%02X", (unsigned)addrs[i]);
            break;
        }
        err = ESP_ERR_NOT_FOUND;
    }

    if (!s_present) {
        ESP_LOGW(TAG, "QMI8658A not found (0x6A/0x68); check SA0 wiring");
        return ESP_ERR_NOT_FOUND;
    }

    /* 使能 accel+gyro；量程 aFS=+/-4g（寄存器映射上板实测核对） */
    (void)s_write_reg(QMI8658_REG_CTRL6, QMI8658_CTRL6_ACC_GYR);
    uint8_t ctrl2 = 0U;
    if (s_read_reg(QMI8658_REG_CTRL2, &ctrl2, 1U) == ESP_OK) {
        (void)s_write_reg(QMI8658_REG_CTRL2, (uint8_t)((ctrl2 & 0xCFU) | 0x10U));
    }
    return ESP_OK;
}

static esp_err_t s_read_axis(uint8_t reg_l, bsp_imu_vec3_t *out, int32_t lsb_per_unit)
{
    if ((out == NULL) || !s_present) {
        return ESP_ERR_NOT_FOUND;
    }
    uint8_t buf[QMI8658_AXIS_BYTES];
    esp_err_t err = s_read_reg(reg_l, buf, sizeof(buf));
    if (err != ESP_OK) {
        return err;
    }
    const int16_t x = (int16_t)((uint16_t)buf[0] | ((uint16_t)buf[1] << 8U));
    const int16_t y = (int16_t)((uint16_t)buf[2] | ((uint16_t)buf[3] << 8U));
    const int16_t z = (int16_t)((uint16_t)buf[4] | ((uint16_t)buf[5] << 8U));
    out->x = ((int32_t)x * 1000) / lsb_per_unit;
    out->y = ((int32_t)y * 1000) / lsb_per_unit;
    out->z = ((int32_t)z * 1000) / lsb_per_unit;
    return ESP_OK;
}

esp_err_t bsp_imu_read_accel(bsp_imu_vec3_t *accel)
{
    return s_read_axis(QMI8658_REG_ACC_X_L, accel, ACCEL_LSB_PER_G);
}

esp_err_t bsp_imu_read_gyro(bsp_imu_vec3_t *gyro)
{
    return s_read_axis(QMI8658_REG_GYR_X_L, gyro, GYRO_LSB_PER_DPS);
}

bool bsp_imu_is_present(void)
{
    return s_present;
}


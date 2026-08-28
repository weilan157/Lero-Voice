/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file bsp_imu.c
 * @brief QMI8658A 6-axis IMU polling driver (I2C0: SDA=IO0 / SCL=IO1).
 *
 * Register map per QMI8658A datasheet (verified against QMI8658A_REV_0.9):
 *   WHO_AM_I 0x00 (=0x05) | CTRL1 0x02 (accel ODR/FS) | CTRL2 0x03 (gyro
 *   ODR/FS) | CTRL7 0x08 (AEN bit0 / GEN bit1 / EN_L bit3) |
 *   STATUS0 0x2E (DATA_RDY bit0) | TEMP 0x33/0x34 |
 *   AX_L..AZ_H 0x35..0x3A | GX_L..GZ_H 0x3B..0x40 (16-bit LE).
 *
 * INT1/INT2 not wired (PLAN 2.6 #6) -> polling; SA0 floats -> probe
 * 0x6A / 0x68 (PLAN 2.6 #7). Values: accel milli-g, gyro milli-dps.
 */

#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bsp_config.h"
#include "bsp_i2c.h"
#include "bsp_imu.h"

#define TAG "bsp_imu"

/* ---------------- QMI8658A registers ---------------- */
#define QMI8658_REG_WHO_AM_I    0x00U
#define QMI8658_WHO_AM_I_VALUE  0x05U
#define QMI8658_REG_CTRL1       0x02U   /* accel: AODR[6:4], AFS[2:0] */
#define QMI8658_REG_CTRL2       0x03U   /* gyro:  GODR[6:4], GFS[2:0] */
#define QMI8658_REG_CTRL7       0x08U   /* AEN(0) GEN(1) EN_L(3) */
#define QMI8658_REG_STATUS0     0x2EU   /* DATA_RDY bit0 */
#define QMI8658_REG_TEMP_L      0x33U
#define QMI8658_REG_ACC_X_L     0x35U
#define QMI8658_REG_GYR_X_L     0x3BU
#define QMI8658_AXIS_BYTES      6U

/* ---------------- 配置（按量程选择，供上层 Kconfig 扩展） ---------------- */
/* accel: ±4g, ODR 1000 Hz（AODR=0b011） */
#define QMI8658_AODR_1000HZ     (0b011U << 4U)
#define QMI8658_AFS_4G          (0b001U)
#define QMI8658_CTRL1_CFG       (QMI8658_AODR_1000HZ | QMI8658_AFS_4G)
#define ACCEL_LSB_PER_G         8192    /* ±4g / 32768 */

/* gyro: ±2048 dps, ODR 1000 Hz（GODR=0b011） */
#define QMI8658_GODR_1000HZ     (0b011U << 4U)
#define QMI8658_GFS_2048DPS     (0b111U)
#define QMI8658_CTRL2_CFG       (QMI8658_GODR_1000HZ | QMI8658_GFS_2048DPS)
#define GYRO_LSB_PER_DPS        16      /* 2048 / 32768 */

#define QMI8658_CTRL7_ENABLE    (0b00000011U)   /* AEN + GEN */

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
    if (s_present) {
        return ESP_OK;
    }
    const uint16_t addrs[2] = { BSP_QMI8658_ADDR_A, BSP_QMI8658_ADDR_B };
    esp_err_t err = ESP_ERR_NOT_FOUND;

    for (size_t i = 0U; i < 2U; i++) {
        i2c_master_dev_handle_t dev = NULL;
        err = bsp_i2c_add_device0(addrs[i], BSP_I2C0_FREQ_HZ, &dev);
        if (err != ESP_OK) {
            continue;
        }
        uint8_t reg = QMI8658_REG_WHO_AM_I;
        uint8_t who = 0U;
        err = i2c_master_transmit_receive(dev, &reg, 1U, &who, 1U, 100);
        if ((err == ESP_OK) && (who == QMI8658_WHO_AM_I_VALUE)) {
            s_dev = dev;
            s_present = true;
            ESP_LOGI(TAG, "QMI8658A found at 0x%02X", (unsigned)addrs[i]);
            break;
        }
        /* 探测失败：立即移除句柄（避免总线上残留 + 重试累积泄漏） */
        (void)i2c_master_bus_rm_device(dev);
        err = ESP_ERR_NOT_FOUND;
    }

    if (!s_present) {
        ESP_LOGW(TAG, "QMI8658A not found (0x6A/0x6B); check SA0 wiring");
        return ESP_ERR_NOT_FOUND;
    }

    /* accel ±4g / 1 kHz，gyro ±2048 dps / 1 kHz，使能双传感器 */
    (void)s_write_reg(QMI8658_REG_CTRL1, QMI8658_CTRL1_CFG);
    (void)s_write_reg(QMI8658_REG_CTRL2, QMI8658_CTRL2_CFG);
    (void)s_write_reg(QMI8658_REG_CTRL7, QMI8658_CTRL7_ENABLE);
    vTaskDelay(pdMS_TO_TICKS(20U));     /* 稳定后首次读数有效 */
    ESP_LOGI(TAG, "imu ready (accel ±4g/1kHz, gyro ±2048dps/1kHz, polling)");
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

esp_err_t bsp_imu_read_temp(int32_t *temp_milli_c)
{
    if ((temp_milli_c == NULL) || !s_present) {
        return ESP_ERR_NOT_FOUND;
    }
    uint8_t buf[2];
    esp_err_t err = s_read_reg(QMI8658_REG_TEMP_L, buf, sizeof(buf));
    if (err != ESP_OK) {
        return err;
    }
    const int16_t raw = (int16_t)((uint16_t)buf[0] | ((uint16_t)buf[1] << 8U));
    /* TEMP ≈ raw × 0.010 °C（QMI8658A 温度分辨率；绝对精度需标定） */
    *temp_milli_c = ((int32_t)raw * 10);
    return ESP_OK;
}

bool bsp_imu_is_present(void)
{
    return s_present;
}

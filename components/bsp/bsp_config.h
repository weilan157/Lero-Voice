/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file bsp_config.h
 * @brief Board level configuration - THE single board adaptation point.
 *
 * Pin mapping verified network-by-network against
 * docs/SCH_Schematic1_1_2026-08-25.pdf (see docs/PLAN.md section 2.4).
 * Changing board only requires editing this file and the matching bsp_*.c.
 */

#ifndef BSP_CONFIG_H
#define BSP_CONFIG_H

#include <stdint.h>
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- I2C0: ES8389 codec + QMI8658A IMU (PLAN 2.4.1) -------- */
#define BSP_I2C0_SDA_GPIO           GPIO_NUM_0
#define BSP_I2C0_SCL_GPIO           GPIO_NUM_1
#define BSP_I2C0_FREQ_HZ            400000U

/* ---------------- I2C1: capacitive touch (PLAN 2.4.3) ------------------- */
#define BSP_I2C1_SDA_GPIO           GPIO_NUM_46
#define BSP_I2C1_SCL_GPIO           GPIO_NUM_47
#define BSP_I2C1_FREQ_HZ            400000U
#define BSP_TOUCH_INT_GPIO          GPIO_NUM_2
#define BSP_TOUCH_RST_GPIO          GPIO_NUM_48   /* 原理图标注 IO49，疑误，见 PLAN 2.6 #3 */

/* ---------------- I2S: audio (PLAN 2.4.1) ------------------------------- */
#define BSP_I2S_SCLK_GPIO           GPIO_NUM_3
#define BSP_I2S_LRCK_GPIO           GPIO_NUM_4
#define BSP_I2S_SDOUT_GPIO          GPIO_NUM_5
#define BSP_I2S_DSDIN_GPIO          GPIO_NUM_6
/* MCLK：本板无此网络（用户确认 2026-08-27；IO36 已给屏幕 DB14）。
 * 采用官方驱动的 **BCLK PIN 模式**（no_mclk=true）：ES8389 内部时钟从
 * I2S BCLK 派生，无需外部 MCLK，也无需 I2S 输出 MCLK（I2S_GPIO_UNUSED）。 */
/* #define BSP_I2S_MCLK_GPIO        GPIO_NUM_36 */

/* ---------------- Amplifier: NS4150B x2 (PLAN 2.4.1) -------------------- */
#define BSP_PA_CTRL_GPIO            GPIO_NUM_52

/* ---------------- LCD: 18-bit RGB8080 interface (PLAN 2.4.2) ------------ */
#define BSP_LCD_DB0_GPIO            GPIO_NUM_7
#define BSP_LCD_DB1_GPIO            GPIO_NUM_8
#define BSP_LCD_DB2_GPIO            GPIO_NUM_9
#define BSP_LCD_DB3_GPIO            GPIO_NUM_10
#define BSP_LCD_DB4_GPIO            GPIO_NUM_11
#define BSP_LCD_DB5_GPIO            GPIO_NUM_12
#define BSP_LCD_DB6_GPIO            GPIO_NUM_13
#define BSP_LCD_DB7_GPIO            GPIO_NUM_14
#define BSP_LCD_DB8_GPIO            GPIO_NUM_15
#define BSP_LCD_DB9_GPIO            GPIO_NUM_16
#define BSP_LCD_DB10_GPIO           GPIO_NUM_17
#define BSP_LCD_DB11_GPIO           GPIO_NUM_18
#define BSP_LCD_DB12_GPIO           GPIO_NUM_19
#define BSP_LCD_DB13_GPIO           GPIO_NUM_35   /* 2026-08-27 按用户清单：DB13~15=IO35/36/37 */
#define BSP_LCD_DB14_GPIO           GPIO_NUM_36
#define BSP_LCD_DB15_GPIO           GPIO_NUM_37
#define BSP_LCD_DB16_GPIO           GPIO_NUM_38
#define BSP_LCD_DB17_GPIO           GPIO_NUM_39
#define BSP_LCD_PCLK_GPIO           GPIO_NUM_40
#define BSP_LCD_DE_GPIO             GPIO_NUM_42
#define BSP_LCD_RESET_GPIO          GPIO_NUM_43
#define BSP_LCD_HS_GPIO             GPIO_NUM_44
#define BSP_LCD_VS_GPIO             GPIO_NUM_45
#define BSP_LCD_BL_GPIO             GPIO_NUM_54

/* ---------------- SD card: module dedicated SDIO pins ------------------- */
/* 使用 SDMMC_SLOT_CONFIG_DEFAULT()（S31 默认引脚映射，见 PLAN 2.4.4）     */

/* ---------------- Buttons: SW3~5, 10k pull-up, active low (PLAN 2.4.5) -- */
#define BSP_BTN1_GPIO               GPIO_NUM_55
#define BSP_BTN2_GPIO               GPIO_NUM_56
#define BSP_BTN3_GPIO               GPIO_NUM_57
#define BSP_BTN_ACTIVE_LEVEL        0   /* 按下为低电平 */

/* ---------------- Power / status (PLAN 2.4.7) --------------------------- */
#define BSP_BAT_ADC_GPIO            GPIO_NUM_50
#define BSP_BUS_ADC_GPIO            GPIO_NUM_51
#define BSP_LED_STATUS_GPIO         GPIO_NUM_49   /* 低电平点亮 */
#define BSP_LED_ACTIVE_LEVEL        0

/* ---------------- USB load switch (PLAN 2.4.6) -------------------------- */
#define BSP_USB_EN_GPIO             GPIO_NUM_53

/* ---------------- I2C device addresses (PLAN 2.4.1 / 2.6 #7) ------------ */
/* 地址语义（2026-08-28 上板实测修正）：
 * - BSP_ES8389_I2C_ADDR = 0x20 是 **8-bit 写地址**，与官方驱动
 *   ES8389_CODEC_DEFAULT_ADDR 一致；esp_codec_dev 的 ctrl 层内部
 *   >>1 得 7-bit 0x10（audio_codec_ctrl_i2c.c），因此传给
 *   esp_codec_dev 的 .addr 保持 0x20。
 * - i2c_master 总线（bsp_i2c_add_device0 / diag i2c-scan / reg）使用
 *   **7-bit 地址 0x10**（i2c-scan 实测芯片在 7-bit 0x10 = 8-bit 0x20）。 */
#define BSP_ES8389_I2C_ADDR         0x20U   /* 8-bit 写地址（esp_codec_dev 用） */
#define BSP_ES8389_I2C_ADDR_7BIT    0x10U   /* 7-bit 地址（i2c_master 总线用） */
#define BSP_QMI8658_ADDR_A          0x6AU
#define BSP_QMI8658_ADDR_B          0x68U

#ifdef __cplusplus
}
#endif

#endif /* BSP_CONFIG_H */


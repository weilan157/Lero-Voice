/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief ESP BSP: ESP32-S31-Korvo
 */

#pragma once

#include "driver/gpio.h"
#include "bsp/audio.h"
#include "bsp/button.h"
#include "bsp/camera.h"
#include "bsp/display.h"
#include "bsp/led.h"
#include "bsp/sdcard.h"

#ifdef __cplusplus
extern "C" {
#endif

/**************************************************************************************************
 * BSP Board Name
 **************************************************************************************************/

#define BSP_BOARD_ESP32_S31_KORVO

/**************************************************************************************************
 * BSP Capabilities
 **************************************************************************************************/

#define BSP_CAPS_DISPLAY          1
#define BSP_CAPS_TOUCH            1
#define BSP_CAPS_BUTTONS          1
#define BSP_CAPS_AUDIO            1
#define BSP_CAPS_AUDIO_SPEAKER    1
#define BSP_CAPS_AUDIO_MIC        1
#define BSP_CAPS_LED              1
#define BSP_CAPS_CAMERA           1
#define BSP_CAPS_SDCARD           1

/**************************************************************************************************
 * Shared I2C
 **************************************************************************************************/

#define BSP_I2C_SDA               (GPIO_NUM_0)
#define BSP_I2C_SCL               (GPIO_NUM_1)

/**************************************************************************************************
 * RGB LCD
 **************************************************************************************************/

#define BSP_LCD_DATA0             (GPIO_NUM_8)   /* B3 */
#define BSP_LCD_DATA1             (GPIO_NUM_9)   /* B4 */
#define BSP_LCD_DATA2             (GPIO_NUM_10)  /* B5 */
#define BSP_LCD_DATA3             (GPIO_NUM_11)  /* B6 */
#define BSP_LCD_DATA4             (GPIO_NUM_12)  /* B7 */
#define BSP_LCD_DATA5             (GPIO_NUM_13)  /* G2 */
#define BSP_LCD_DATA6             (GPIO_NUM_14)  /* G3 */
#define BSP_LCD_DATA7             (GPIO_NUM_15)  /* G4 */
#define BSP_LCD_DATA8             (GPIO_NUM_16)  /* G5 */
#define BSP_LCD_DATA9             (GPIO_NUM_17)  /* G6 */
#define BSP_LCD_DATA10            (GPIO_NUM_18)  /* G7 */
#define BSP_LCD_DATA11            (GPIO_NUM_19)  /* R3 */
#define BSP_LCD_DATA12            (GPIO_NUM_33)  /* R4 */
#define BSP_LCD_DATA13            (GPIO_NUM_34)  /* R5 */
#define BSP_LCD_DATA14            (GPIO_NUM_35)  /* R6 */
#define BSP_LCD_DATA15            (GPIO_NUM_36)  /* R7 */
#define BSP_LCD_PCLK              (GPIO_NUM_40)
#define BSP_LCD_DE                (GPIO_NUM_43)
#define BSP_LCD_HSYNC             (GPIO_NUM_44)
#define BSP_LCD_VSYNC             (GPIO_NUM_45)

/* LCD sub-board control signals are routed but not required by the RGB panel driver yet. */
#define BSP_LCD_CS                (GPIO_NUM_38)
#define BSP_LCD_MOSI              (GPIO_NUM_60)
#define BSP_LCD_SCK               (GPIO_NUM_61)
#define BSP_LCD_BACKLIGHT         (GPIO_NUM_NC)
#define BSP_LCD_DISP_EN           (GPIO_NUM_NC)

/**************************************************************************************************
 * LCD Touch
 **************************************************************************************************/

#define BSP_LCD_TOUCH_I2C_SDA     BSP_I2C_SDA
#define BSP_LCD_TOUCH_I2C_SCL     BSP_I2C_SCL
#define BSP_LCD_TOUCH_RST         (GPIO_NUM_NC)
#define BSP_LCD_TOUCH_INT         (GPIO_NUM_NC)

/**************************************************************************************************
 * DVP Camera (reserved for the next BSP increment)
 **************************************************************************************************/

#define BSP_CAMERA_SCCB_SDA       (GPIO_NUM_0)
#define BSP_CAMERA_SCCB_SCL       (GPIO_NUM_1)
#define BSP_CAMERA_D0             (GPIO_NUM_46)
#define BSP_CAMERA_D1             (GPIO_NUM_47)
#define BSP_CAMERA_D2             (GPIO_NUM_48)
#define BSP_CAMERA_D3             (GPIO_NUM_49)
#define BSP_CAMERA_D4             (GPIO_NUM_50)
#define BSP_CAMERA_D5             (GPIO_NUM_51)
#define BSP_CAMERA_D6             (GPIO_NUM_52)
#define BSP_CAMERA_D7             (GPIO_NUM_53)
#define BSP_CAMERA_PCLK           (GPIO_NUM_54)
#define BSP_CAMERA_XCLK           (GPIO_NUM_55)
#define BSP_CAMERA_VSYNC          (GPIO_NUM_56)
#define BSP_CAMERA_HSYNC          (GPIO_NUM_57)
#define BSP_CAMERA_PWDN           (GPIO_NUM_NC)
#define BSP_CAMERA_RESET          (GPIO_NUM_NC)
#define BSP_CAMERA_GM_FK          (GPIO_NUM_38)

/**************************************************************************************************
 * SD Card
 *
 * GPIO20~GPIO25 and GPIO39 are used for SDMMC 4-bit bus.
 *
 * Note: GPIO20~GPIO25 are shared with the SPI NAND flash footprint. Only one can be populated
 * at a time per board stuffing option.
 **************************************************************************************************/

#define BSP_SD_D0                 (GPIO_NUM_20)
#define BSP_SD_D1                 (GPIO_NUM_21)
#define BSP_SD_D2                 (GPIO_NUM_22)
#define BSP_SD_D3                 (GPIO_NUM_23)
#define BSP_SD_CLK                (GPIO_NUM_24)
#define BSP_SD_CMD                (GPIO_NUM_25)
#define BSP_SD_CTRL               (GPIO_NUM_39)   /* Active-low power/switch control */
#define BSP_SD_CTRL_ACTIVE_LEVEL  (0)
#define BSP_SD_CTRL_INACTIVE_LEVEL (!BSP_SD_CTRL_ACTIVE_LEVEL)

/**************************************************************************************************
 * Audio
 **************************************************************************************************/

#define BSP_AUDIO_I2S_MCLK        (GPIO_NUM_2)
#define BSP_AUDIO_I2S_SCLK        (GPIO_NUM_3)
#define BSP_AUDIO_I2S_LRCLK       (GPIO_NUM_4)
#define BSP_AUDIO_I2S_DSIN        (GPIO_NUM_6)
#define BSP_AUDIO_I2S_SDOUT       (GPIO_NUM_5)
#define BSP_AUDIO_PA_CTRL         (GPIO_NUM_7)

/**************************************************************************************************
 * Buttons and LED
 **************************************************************************************************/

#define BSP_BUTTONS_ADC           (GPIO_NUM_42)
#define BSP_LED_WS2812            (GPIO_NUM_37)

#ifdef __cplusplus
}
#endif

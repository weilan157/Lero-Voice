/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief ESP32-S31-Korvo BSP camera interface
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup camera Camera
 *  @brief DVP camera BSP API
 *  @{
 */

#define BSP_CAMERA_DEFAULT_XCLK_FREQ_HZ      (20 * 1000 * 1000)
#define BSP_CAMERA_DEFAULT_SCCB_FREQ_HZ      (10 * 1000)
#define BSP_CAMERA_DEFAULT_WIDTH             (1280)
#define BSP_CAMERA_DEFAULT_HEIGHT            (720)
#define BSP_CAMERA_BUFFER_COUNT              (3)

typedef struct bsp_camera_t bsp_camera_t;

typedef enum {
    BSP_CAMERA_PIXEL_FORMAT_RGB565 = 0,
    BSP_CAMERA_PIXEL_FORMAT_RGB565_BE,
    BSP_CAMERA_PIXEL_FORMAT_JPEG,
} bsp_camera_pixel_format_t;

typedef struct {
    uint32_t width;                         /*!< Capture width in pixels */
    uint32_t height;                        /*!< Capture height in pixels */
    bsp_camera_pixel_format_t pixel_format; /*!< Capture pixel format */
    uint32_t xclk_freq_hz;                  /*!< DVP XCLK frequency */
} bsp_camera_config_t;

typedef struct {
    void *data;      /*!< Captured frame data */
    size_t size;     /*!< Captured frame size in bytes */
    uint32_t index;  /*!< Internal V4L2 buffer index */
} bsp_camera_frame_t;

typedef struct {
    uint32_t width;        /*!< Negotiated capture width in pixels */
    uint32_t height;       /*!< Negotiated capture height in pixels */
    uint32_t pixelformat;  /*!< Negotiated V4L2 pixel format */
    uint32_t bytesperline; /*!< Negotiated source stride in bytes */
    uint32_t sizeimage;    /*!< Negotiated frame buffer size in bytes */
} bsp_camera_format_t;

#define BSP_CAMERA_DEFAULT_CONFIG() {                         \
    .width = BSP_CAMERA_DEFAULT_WIDTH,                        \
    .height = BSP_CAMERA_DEFAULT_HEIGHT,                      \
    .pixel_format = BSP_CAMERA_PIXEL_FORMAT_JPEG,             \
    .xclk_freq_hz = BSP_CAMERA_DEFAULT_XCLK_FREQ_HZ,          \
}

/**
 * @brief Open and configure the DVP camera capture device.
 *
 * @param[in] config Camera configuration. NULL selects BSP_CAMERA_DEFAULT_CONFIG().
 * @param[out] ret_camera Returned camera handle.
 *
 * @return ESP_OK on success, otherwise an ESP-IDF error code.
 */
esp_err_t bsp_camera_open(const bsp_camera_config_t *config, bsp_camera_t **ret_camera);

/**
 * @brief Start camera streaming.
 */
esp_err_t bsp_camera_start(bsp_camera_t *camera);

/**
 * @brief Set camera JPEG compression quality.
 *
 * @param camera Camera handle.
 * @param quality JPEG compression quality value passed to the sensor driver.
 *
 * @return ESP_OK on success, ESP_ERR_NOT_SUPPORTED if the sensor does not support it.
 */
esp_err_t bsp_camera_set_jpeg_quality(bsp_camera_t *camera, int quality);

/**
 * @brief Set camera image orientation by sensor-side horizontal mirror and vertical flip.
 *
 * @param camera Camera handle.
 * @param horizontal_mirror Enable horizontal mirror when true.
 * @param vertical_flip Enable vertical flip when true.
 *
 * @return ESP_OK on success, ESP_ERR_NOT_SUPPORTED if the sensor/driver rejects the controls.
 */
esp_err_t bsp_camera_set_orientation(bsp_camera_t *camera, bool horizontal_mirror, bool vertical_flip);

/**
 * @brief Stop camera streaming.
 */
esp_err_t bsp_camera_stop(bsp_camera_t *camera);

/**
 * @brief Dequeue one captured frame.
 *
 * The frame must be returned with bsp_camera_return_frame() after use.
 */
esp_err_t bsp_camera_get_frame(bsp_camera_t *camera, bsp_camera_frame_t *frame);

/**
 * @brief Return a previously dequeued frame to the camera driver.
 */
esp_err_t bsp_camera_return_frame(bsp_camera_t *camera, const bsp_camera_frame_t *frame);

/**
 * @brief Get the negotiated camera capture format.
 */
esp_err_t bsp_camera_get_format(bsp_camera_t *camera, bsp_camera_format_t *out_format);

/**
 * @brief Close the camera device and release its buffers.
 */
esp_err_t bsp_camera_close(bsp_camera_t *camera);

/** @} */

#ifdef __cplusplus
}
#endif

/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "factory_demo_camera.h"

#include <inttypes.h>
#include <string.h>
#include "bsp/esp32_s31_korvo.h"
#include "esp_heap_caps.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define FACTORY_CAMERA_SENSOR_WIDTH       240
#define FACTORY_CAMERA_SENSOR_HEIGHT      240
#define FACTORY_CAMERA_TASK_STACK         8192
#define FACTORY_CAMERA_TASK_PRIORITY      6

static const char *TAG = "factory_camera";

static void factory_camera_set_status_locked(factory_camera_t *camera, factory_camera_state_t state, const char *status)
{
    camera->state = state;
    strlcpy(camera->status, status, sizeof(camera->status));
}

static void factory_camera_set_status(factory_camera_t *camera, factory_camera_state_t state, const char *status)
{
    xSemaphoreTake(camera->lock, portMAX_DELAY);
    factory_camera_set_status_locked(camera, state, status);
    xSemaphoreGive(camera->lock);
}

static esp_err_t factory_camera_register_ppa(factory_camera_t *camera)
{
    if (camera->ppa_srm) {
        return ESP_OK;
    }

    const ppa_client_config_t ppa_config = {
        .oper_type = PPA_OPERATION_SRM,
        .max_pending_trans_num = 1,
    };
    ESP_RETURN_ON_ERROR(ppa_register_client(&ppa_config, &camera->ppa_srm), TAG, "register PPA SRM client failed");
    ESP_LOGI(TAG, "PPA SRM client registered");
    return ESP_OK;
}

/* Scale the RGB565_BE camera frame up to the full-screen canvas buffer using PPA.
 * byte_swap converts the big-endian sensor data to the little-endian RGB565 the
 * LVGL canvas expects; a 180 degree rotation keeps the preview upright.
 */
static esp_err_t factory_camera_scale_to_canvas(factory_camera_t *camera,
                                                const bsp_camera_format_t *format,
                                                const bsp_camera_frame_t *frame)
{
    const uint32_t src_w = format->width;
    const uint32_t src_h = format->height;
    const uint32_t dst_w = camera->canvas_width;
    const uint32_t dst_h = camera->canvas_height;
    const uint32_t src_pic_w = format->bytesperline ? (format->bytesperline / (uint32_t)sizeof(uint16_t)) : src_w;

    ppa_srm_oper_config_t srm_config = {
        .in = {
            .buffer = frame->data,
            .pic_w = src_pic_w,
            .pic_h = src_h,
            .block_w = src_w,
            .block_h = src_h,
            .block_offset_x = 0,
            .block_offset_y = 0,
            .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        },
        .out = {
            .buffer = camera->canvas_buf,
            .buffer_size = camera->canvas_stride * dst_h,
            .pic_w = dst_w,
            .pic_h = dst_h,
            .block_offset_x = 0,
            .block_offset_y = 0,
            .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        },
        .rotation_angle = PPA_SRM_ROTATION_ANGLE_180,
        .scale_x = (float)dst_w / (float)src_w,
        .scale_y = (float)dst_h / (float)src_h,
        .byte_swap = true,
        .mode = PPA_TRANS_MODE_BLOCKING,
    };

    return ppa_do_scale_rotate_mirror(camera->ppa_srm, &srm_config);
}

static void factory_camera_task(void *arg)
{
    factory_camera_t *camera = (factory_camera_t *)arg;
    bsp_camera_t *bsp_camera = NULL;
    bsp_camera_format_t format = {0};
    bool stream_started = false;
    esp_err_t ret;

    factory_camera_set_status(camera, FACTORY_CAMERA_STATE_STARTING, "Opening DVP camera...");

    const bsp_camera_config_t config = {
        .width = FACTORY_CAMERA_SENSOR_WIDTH,
        .height = FACTORY_CAMERA_SENSOR_HEIGHT,
        .pixel_format = BSP_CAMERA_PIXEL_FORMAT_RGB565_BE,
        .xclk_freq_hz = BSP_CAMERA_DEFAULT_XCLK_FREQ_HZ,
    };

    ret = factory_camera_register_ppa(camera);
    if (ret != ESP_OK) {
        goto cleanup;
    }

    ret = bsp_camera_open(&config, &bsp_camera);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Open BSP camera failed (%s)", esp_err_to_name(ret));
        goto cleanup;
    }
    xSemaphoreTake(camera->lock, portMAX_DELAY);
    camera->bsp_camera = bsp_camera;
    xSemaphoreGive(camera->lock);

    ret = bsp_camera_get_format(bsp_camera, &format);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Get BSP camera format failed (%s)", esp_err_to_name(ret));
        goto cleanup;
    }
    ESP_LOGI(TAG, "Camera format %"PRIu32"x%"PRIu32", stride=%"PRIu32", size=%"PRIu32,
             format.width, format.height, format.bytesperline, format.sizeimage);

    ret = bsp_camera_start(bsp_camera);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Start BSP camera failed (%s)", esp_err_to_name(ret));
        goto cleanup;
    }
    stream_started = true;

    xSemaphoreTake(camera->lock, portMAX_DELAY);
    camera->frame_width = format.width;
    camera->frame_height = format.height;
    camera->pixelformat = format.pixelformat;
    factory_camera_set_status_locked(camera, FACTORY_CAMERA_STATE_PREVIEW, "DVP camera running");
    xSemaphoreGive(camera->lock);

    while (1) {
        xSemaphoreTake(camera->lock, portMAX_DELAY);
        bool stop_requested = camera->stop_requested;
        lv_obj_t *canvas = camera->canvas;
        void *canvas_buf = camera->canvas_buf;
        bool render_enabled = camera->render_enabled;
        xSemaphoreGive(camera->lock);
        if (stop_requested) {
            break;
        }

        bsp_camera_frame_t frame = {0};
        ret = bsp_camera_get_frame(bsp_camera, &frame);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Get BSP camera frame failed (%s)", esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        if (render_enabled && canvas && canvas_buf && frame.data) {
            if (bsp_display_lock(-1)) {
                esp_err_t render_ret = factory_camera_scale_to_canvas(camera, &format, &frame);
                if (render_ret == ESP_OK) {
                    lv_obj_invalidate(canvas);
                } else {
                    ESP_LOGW(TAG, "PPA full-screen scale failed (%s)", esp_err_to_name(render_ret));
                }
                bsp_display_unlock();
            } else {
                ESP_LOGW(TAG, "Skip camera preview because LVGL lock is unavailable");
            }
        }

        ret = bsp_camera_return_frame(bsp_camera, &frame);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Return BSP camera frame failed (%s)", esp_err_to_name(ret));
        }

        xSemaphoreTake(camera->lock, portMAX_DELAY);
        camera->frame_count++;
        xSemaphoreGive(camera->lock);
    }

    ret = ESP_OK;

cleanup:
    if (stream_started) {
        esp_err_t stop_ret = bsp_camera_stop(bsp_camera);
        if (stop_ret != ESP_OK) {
            ESP_LOGW(TAG, "Stop BSP camera failed (%s)", esp_err_to_name(stop_ret));
        }
    }
    xSemaphoreTake(camera->lock, portMAX_DELAY);
    camera->stop_requested = false;
    if (ret == ESP_OK) {
        factory_camera_set_status_locked(camera, FACTORY_CAMERA_STATE_IDLE, "Ready for DVP camera");
    } else {
        factory_camera_set_status_locked(camera, FACTORY_CAMERA_STATE_ERROR, "DVP camera failed");
    }
    camera->task = NULL;
    xSemaphoreGive(camera->lock);

    if (camera->ppa_srm) {
        esp_err_t ppa_ret = ppa_unregister_client(camera->ppa_srm);
        if (ppa_ret != ESP_OK) {
            ESP_LOGW(TAG, "Unregister PPA SRM client failed (%s)", esp_err_to_name(ppa_ret));
        }
        camera->ppa_srm = NULL;
    }

    vTaskDelete(NULL);
}

esp_err_t factory_camera_init(factory_camera_t *camera, lv_display_t *disp)
{
    ESP_RETURN_ON_FALSE(camera, ESP_ERR_INVALID_ARG, TAG, "invalid camera init arguments");

    memset(camera, 0, sizeof(*camera));
    camera->lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(camera->lock, ESP_ERR_NO_MEM, TAG, "camera mutex alloc failed");
    camera->disp = disp;
    camera->render_enabled = true;
    factory_camera_set_status(camera, FACTORY_CAMERA_STATE_IDLE, "Ready for DVP camera");

    ESP_LOGI(TAG, "Camera preview service initialized");
    return ESP_OK;
}

esp_err_t factory_camera_attach_canvas(factory_camera_t *camera, lv_obj_t *canvas, void *canvas_buf,
                                       uint32_t canvas_width, uint32_t canvas_height, uint32_t canvas_stride)
{
    ESP_RETURN_ON_FALSE(camera && canvas && canvas_buf, ESP_ERR_INVALID_ARG, TAG, "invalid camera canvas arguments");

    xSemaphoreTake(camera->lock, portMAX_DELAY);
    camera->canvas = canvas;
    camera->canvas_buf = canvas_buf;
    camera->canvas_width = canvas_width;
    camera->canvas_height = canvas_height;
    camera->canvas_stride = canvas_stride;
    xSemaphoreGive(camera->lock);

    ESP_LOGI(TAG, "Camera canvas attached: %"PRIu32"x%"PRIu32", stride=%"PRIu32,
             canvas_width, canvas_height, canvas_stride);
    return ESP_OK;
}

esp_err_t factory_camera_start_preview(factory_camera_t *camera)
{
    ESP_RETURN_ON_FALSE(camera, ESP_ERR_INVALID_ARG, TAG, "camera handle is null");

    xSemaphoreTake(camera->lock, portMAX_DELAY);
    if (camera->task || camera->state == FACTORY_CAMERA_STATE_STARTING || camera->state == FACTORY_CAMERA_STATE_PREVIEW) {
        xSemaphoreGive(camera->lock);
        return ESP_ERR_INVALID_STATE;
    }

    camera->stop_requested = false;
    camera->frame_count = 0;
    camera->frame_width = 0;
    camera->frame_height = 0;
    camera->pixelformat = 0;
    factory_camera_set_status_locked(camera, FACTORY_CAMERA_STATE_STARTING, "Starting DVP camera...");
    xSemaphoreGive(camera->lock);

    BaseType_t ok = xTaskCreate(factory_camera_task, "factory_camera", FACTORY_CAMERA_TASK_STACK,
                                camera, FACTORY_CAMERA_TASK_PRIORITY, &camera->task);
    if (ok != pdPASS) {
        xSemaphoreTake(camera->lock, portMAX_DELAY);
        factory_camera_set_status_locked(camera, FACTORY_CAMERA_STATE_ERROR, "Preview task create failed");
        camera->task = NULL;
        xSemaphoreGive(camera->lock);
        ESP_LOGE(TAG, "Camera task create failed");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Camera preview requested");
    return ESP_OK;
}

esp_err_t factory_camera_stop_preview(factory_camera_t *camera)
{
    ESP_RETURN_ON_FALSE(camera, ESP_ERR_INVALID_ARG, TAG, "camera handle is null");

    xSemaphoreTake(camera->lock, portMAX_DELAY);
    if (!camera->task) {
        xSemaphoreGive(camera->lock);
        return ESP_OK;
    }

    camera->stop_requested = true;
    xSemaphoreGive(camera->lock);

    ESP_LOGI(TAG, "Camera stop requested");
    return ESP_OK;
}

esp_err_t factory_camera_set_display(factory_camera_t *camera, lv_display_t *disp)
{
    ESP_RETURN_ON_FALSE(camera, ESP_ERR_INVALID_ARG, TAG, "camera handle is null");

    xSemaphoreTake(camera->lock, portMAX_DELAY);
    camera->disp = disp;
    xSemaphoreGive(camera->lock);
    return ESP_OK;
}

esp_err_t factory_camera_set_render_enabled(factory_camera_t *camera, bool enable)
{
    ESP_RETURN_ON_FALSE(camera, ESP_ERR_INVALID_ARG, TAG, "camera handle is null");

    xSemaphoreTake(camera->lock, portMAX_DELAY);
    camera->render_enabled = enable;
    xSemaphoreGive(camera->lock);
    ESP_LOGI(TAG, "Camera rendering %s", enable ? "enabled" : "disabled");
    return ESP_OK;
}

void factory_camera_get_snapshot(factory_camera_t *camera, factory_camera_snapshot_t *snapshot)
{
    if (!camera || !snapshot) {
        return;
    }

    xSemaphoreTake(camera->lock, portMAX_DELAY);
    snapshot->state = camera->state;
    snapshot->frame_width = camera->frame_width;
    snapshot->frame_height = camera->frame_height;
    snapshot->pixelformat = camera->pixelformat;
    snapshot->frame_count = camera->frame_count;
    strlcpy(snapshot->status, camera->status, sizeof(snapshot->status));
    xSemaphoreGive(camera->lock);
}

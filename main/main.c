/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file main.c
 * @brief Lero Voice application entry (docs/PLAN.md sections 3.2 / 3.5).
 *
 * Layering:  bsp (hardware) -> provisioning / ota_service / diag (components)
 *            -> main (orchestration).
 * The application layer never touches hardware registers directly; all tasks
 * use static stacks / static TCBs (no dynamic memory, docs/PLAN.md 3.4).
 */

#include <stdint.h>
#include <string.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "bsp.h"
#include "bsp_buttons.h"
#include "bsp_power.h"
#include "provisioning.h"
#include "ota_service.h"
#include "player.h"
#include "voice.h"
#include "diag.h"

#define TAG "main"

/* ------------------------------------------------------------------------- */
/* Application event bus (static queue, docs/PLAN.md 3.5.3)                  */
/* ------------------------------------------------------------------------- */

typedef enum {
    APP_EVT_BTN1_LONG = 1,      /*< 功能键1 长按：进入配网 */
    APP_EVT_BTN1_VERY_LONG,     /*< 功能键1 超长按：恢复出厂 */
    APP_EVT_BTN2_LONG,          /*< 功能键2 长按：检查 OTA */
    APP_EVT_BTN2_SHORT,         /*< 功能键2 短按：待确认时=确认升级，否则=检查 OTA */
    APP_EVT_BTN3_SHORT,         /*< 功能键3 短按：状态灯切换 */
    APP_EVT_BTN3_LONG,          /*< 功能键3 长按：开始/停止语音聆听 */
    APP_EVT_OTA_CHECK,          /*< 触发 OTA 检查（HTTP 通道） */
    APP_EVT_OTA_CONFIRM,        /*< 用户确认升级（UI/语音/按键入口） */
    APP_EVT_OTA_ABORT,          /*< 用户取消升级（UI/语音/按键入口） */
} app_event_t;

static StackType_t s_event_queue_storage[CONFIG_LERO_APP_EVENT_QUEUE_LEN];
static StaticQueue_t s_event_queue_tcb;
static QueueHandle_t s_event_queue;
static bool s_led_on;

/* ------------------------------------------------------------------------- */
/* Static task resources (docs/PLAN.md 3.5.1)                                */
/* ------------------------------------------------------------------------- */

static StackType_t s_net_stack[CONFIG_LERO_NET_TASK_STACK_SIZE / sizeof(StackType_t)];
static StaticTask_t s_net_tcb;
static StackType_t s_ota_stack[CONFIG_LERO_OTA_TASK_STACK_SIZE / sizeof(StackType_t)];
static StaticTask_t s_ota_tcb;
static StackType_t s_sys_stack[CONFIG_LERO_SYS_TASK_STACK_SIZE / sizeof(StackType_t)];
static StaticTask_t s_sys_tcb;

static void s_net_task(void *arg);
static void s_ota_task(void *arg);
static void s_sys_task(void *arg);
static void s_prov_event_cb(prov_state_t state, const char *detail);
static void s_ota_event_cb(ota_state_t state, const ota_event_info_t *info);
static void s_buttons_cb(bsp_button_id_t button, bsp_button_event_t event);
static esp_err_t s_init_nvs(void);

/* ------------------------------------------------------------------------- */

static void app_event_post(app_event_t event)
{
    if (s_event_queue == NULL) {
        return;
    }
    (void)xQueueSend(s_event_queue, &event, 0U);
}

static esp_err_t s_init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if ((err == ESP_ERR_NVS_NO_FREE_PAGES) || (err == ESP_ERR_NVS_NEW_VERSION_FOUND)) {
        err = nvs_flash_erase();
        if (err == ESP_OK) {
            err = nvs_flash_init();
        }
    }
    return err;
}

/* ------------------------------------------------------------------------- */
/* Event callbacks                                                           */
/* ------------------------------------------------------------------------- */

static void s_prov_event_cb(prov_state_t state, const char *detail)
{
    ESP_LOGI(TAG, "prov state=%d detail=%s", (int)state, (detail != NULL) ? detail : "-");
}

static void s_ota_event_cb(ota_state_t state, const ota_event_info_t *info)
{
    ESP_LOGI(TAG, "ota state=%d version=%s pct=%u",
             (int)state,
             (info != NULL) ? info->version : "-",
             (info != NULL) ? (unsigned)info->progress_pct : 0U);
}

/* Button handler runs in the esp_timer context; it must only enqueue. */
static void s_buttons_cb(bsp_button_id_t button, bsp_button_event_t event)
{
    if (button == BSP_BTN_ID_1) {
        if (event == BSP_BTN_EVENT_LONG_PRESS) {
            app_event_post(APP_EVT_BTN1_LONG);
        } else if (event == BSP_BTN_EVENT_VERY_LONG_PRESS) {
            app_event_post(APP_EVT_BTN1_VERY_LONG);
        }
    } else if (button == BSP_BTN_ID_2) {
        if (event == BSP_BTN_EVENT_LONG_PRESS) {
            app_event_post(APP_EVT_BTN2_LONG);
        } else if (event == BSP_BTN_EVENT_SHORT_PRESS) {
            app_event_post(APP_EVT_BTN2_SHORT);
        }
    } else if (button == BSP_BTN_ID_3) {
        if (event == BSP_BTN_EVENT_SHORT_PRESS) {
            app_event_post(APP_EVT_BTN3_SHORT);
        } else if (event == BSP_BTN_EVENT_LONG_PRESS) {
            app_event_post(APP_EVT_BTN3_LONG);
        }
    }
}

/* ------------------------------------------------------------------------- */
/* Tasks (static + pinned, docs/PLAN.md 3.5.1)                               */
/* ------------------------------------------------------------------------- */

/* net_task: 配网状态机轮询 + 断线重连（Core 0, 协议核） */
static void s_net_task(void *arg)
{
    (void)arg;
    for (;;) {
        (void)prov_poll();
        vTaskDelay(pdMS_TO_TICKS(CONFIG_LERO_NET_POLL_PERIOD_MS));
    }
}

/* ota_task: 开机 30s 后首次检查，之后按周期检查；事件驱动时立即检查 */
static void s_ota_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(30000U));
    for (;;) {
        uint32_t event = 0U;
        TickType_t wait = pdMS_TO_TICKS(CONFIG_LERO_OTA_CHECK_INTERVAL_MS);
        if (xQueueReceive(s_event_queue, &event, wait) == pdTRUE) {
            switch ((app_event_t)event) {
            case APP_EVT_OTA_CHECK:
                (void)ota_service_check_http();
                break;
            case APP_EVT_OTA_CONFIRM:
                (void)ota_service_confirm();
                break;
            case APP_EVT_OTA_ABORT:
                (void)ota_service_cancel();
                break;
            default:
                break;
            }
        } else {
            /* 周期检查：IDLE 或 FAILED（失败后允许重试，PLAN 8.8） */
            const ota_state_t st = ota_service_get_state();
            if ((st == OTA_STATE_IDLE) || (st == OTA_STATE_FAILED)) {
                (void)ota_service_check_http();
            }
        }
    }
}

/* system_task: 按键事件分发 / 状态灯 / 恢复出厂（docs/PLAN.md 3.5.1） */
static void s_sys_task(void *arg)
{
    (void)arg;
    for (;;) {
        uint32_t event = 0U;
        if (xQueueReceive(s_event_queue, &event, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        switch ((app_event_t)event) {
        case APP_EVT_BTN1_LONG:
            ESP_LOGI(TAG, "enter provisioning (SmartConfig)");
            (void)prov_enter_smartconfig();
            break;
        case APP_EVT_BTN1_VERY_LONG:
            ESP_LOGW(TAG, "factory reset requested");
            (void)prov_factory_reset();
            break;
        case APP_EVT_BTN2_LONG:
            app_event_post(APP_EVT_OTA_CHECK);
            break;
        case APP_EVT_BTN2_SHORT:
            /* 待确认时短按 = 确认升级；否则 = 检查更新 */
            if (ota_service_get_state() == OTA_STATE_PENDING_APPLY) {
                app_event_post(APP_EVT_OTA_CONFIRM);
            } else {
                app_event_post(APP_EVT_OTA_CHECK);
            }
            break;
        case APP_EVT_BTN3_SHORT:
            s_led_on = !s_led_on;
            (void)bsp_power_set_led(s_led_on);
            break;
        case APP_EVT_BTN3_LONG: {
            /* 语音聆听开关（骨架阶段：按键触发；M9 接入唤醒词） */
            voice_state_t vst = VOICE_STATE_IDLE;
            (void)voice_get_state(&vst);
            if (vst == VOICE_STATE_LISTENING) {
                (void)voice_listen_stop();
            } else {
                (void)voice_listen_start();
            }
            break;
        }
        default:
            break;
        }
    }
}

/* ------------------------------------------------------------------------- */

void app_main(void)
{
    ESP_LOGI(TAG, "Lero Voice %s boot (IDF %s)", bsp_get_version(), IDF_VER);

    if (s_init_nvs() != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed; continue with degraded services");
    }

    /* 事件队列先于 BSP 创建：按键回调只入队，不关心队列是否已就绪 */
    s_event_queue = xQueueCreateStatic(CONFIG_LERO_APP_EVENT_QUEUE_LEN,
                                       sizeof(uint32_t),
                                       s_event_queue_storage,
                                       &s_event_queue_tcb);
    if (s_event_queue == NULL) {
        ESP_LOGE(TAG, "event queue create failed");
    }

    /* BSP 先行（允许部分外设失败，故障位图供 diag 展示） */
    (void)bsp_init();

    (void)diag_init();
    (void)prov_init(s_prov_event_cb);
    (void)ota_service_init(s_ota_event_cb);
    (void)bsp_buttons_set_handler(s_buttons_cb);

    /* SD 卡音乐播放器（ESP-GMF + ES8389；console: play/stop/vol，见 diag） */
    if (player_init() != ESP_OK) {
        ESP_LOGW(TAG, "player init failed (codec/SD 未就绪时降级运行)");
    }

    /* 语音助手骨架（采集/VAD/上传接口；console: voice-*，见 diag） */
    if (voice_init() != ESP_OK) {
        ESP_LOGW(TAG, "voice init failed (降级：无语音采集)");
    }

    /* 开机自动连接已保存的 WiFi 配置；无配置则自动进入配网模式（PLAN 4.3） */
    esp_err_t prov_err = prov_start();
    if (prov_err != ESP_OK) {
        ESP_LOGE(TAG, "prov_start failed: %s", esp_err_to_name(prov_err));
    }

    /* 启动静态任务（优先级/核绑定见 docs/PLAN.md 3.5.1） */
    xTaskCreateStaticPinnedToCore(s_net_task, "net_task", sizeof(s_net_stack), NULL,
                                  CONFIG_LERO_NET_TASK_PRIORITY,
                                  s_net_stack, &s_net_tcb, CONFIG_LERO_NET_TASK_CORE);
    xTaskCreateStaticPinnedToCore(s_ota_task, "ota_task", sizeof(s_ota_stack), NULL,
                                  CONFIG_LERO_OTA_TASK_PRIORITY,
                                  s_ota_stack, &s_ota_tcb, CONFIG_LERO_OTA_TASK_CORE);
    xTaskCreateStaticPinnedToCore(s_sys_task, "sys_task", sizeof(s_sys_stack), NULL,
                                  CONFIG_LERO_SYS_TASK_PRIORITY,
                                  s_sys_stack, &s_sys_tcb, CONFIG_LERO_SYS_TASK_CORE);

    ESP_LOGI(TAG, "app started");
}


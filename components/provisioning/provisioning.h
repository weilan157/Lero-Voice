/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file provisioning.h
 * @brief Wi-Fi provisioning: SmartConfig (ESP-TOUCH v2) with softAP fallback.
 *
 * Flow (docs/PLAN.md section 4.3):
 *   boot -> saved config? connect : SmartConfig (60 s) -> softAP (3 min).
 * The state machine is single-instance; prov_poll() must be called from a
 * task (net_task) to advance timeouts and the network probe.
 */

#ifndef PROVISIONING_H
#define PROVISIONING_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PROV_STATE_UNINIT = 0,
    PROV_STATE_IDLE,            /*< 待机（已联网或未配置） */
    PROV_STATE_CONNECTING,      /*< 正在连接已保存 / SmartConfig 下发的 WiFi */
    PROV_STATE_SCANNING,        /*< SmartConfig 监听中 */
    PROV_STATE_AP_FALLBACK,     /*< softAP 兜底（192.168.4.1 配置页） */
    PROV_STATE_DONE,            /*< 配网完成（已联网） */
} prov_state_t;

typedef struct {
    bool configured;            /*< NVS 中是否有有效 WiFi 配置 */
    char ssid[33];              /*< 已连接 SSID（可能为空） */
    int8_t rssi;                /*< 已连接信号强度 dBm */
    char ip[16];                /*< STA IP 地址 */
    char mac[18];               /*< STA MAC（脱敏展示由上层负责） */
} prov_wifi_status_t;

typedef void (*prov_event_cb_t)(prov_state_t state, const char *detail);

/**
 * @brief Init wifi stack + event handlers (idempotent).
 * @param[in] cb State-change callback (may be NULL).
 * @return ESP_OK on success.
 */
esp_err_t prov_init(prov_event_cb_t cb);

/**
 * @brief Boot entry: connect saved config or enter SmartConfig.
 * @return ESP_OK on success.
 */
esp_err_t prov_start(void);

/**
 * @brief Force entry into SmartConfig mode.
 * @return ESP_OK on success.
 */
esp_err_t prov_enter_smartconfig(void);

/**
 * @brief Force entry into softAP fallback mode.
 * @return ESP_OK on success.
 */
esp_err_t prov_enter_softap(void);

/**
 * @brief Stop any active provisioning session.
 * @return ESP_OK on success.
 */
esp_err_t prov_stop(void);

/**
 * @brief Advance the state machine (timeouts / network probe).
 *        Call periodically from net_task.
 * @return ESP_OK always.
 */
esp_err_t prov_poll(void);

/**
 * @brief Get the current provisioning state.
 * @return Current state.
 */
prov_state_t prov_get_state(void);

/**
 * @brief Get WiFi status snapshot (for diag console / snapshot).
 * @param[out] status Filled status.
 * @return ESP_OK on success.
 */
esp_err_t prov_get_wifi_status(prov_wifi_status_t *status);

/**
 * @brief Factory reset: erase NVS and restart (docs/PLAN.md 4.3 step 5).
 * @return ESP_OK (never returns on success - reboots).
 */
esp_err_t prov_factory_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* PROVISIONING_H */


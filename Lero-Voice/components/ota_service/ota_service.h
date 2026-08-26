/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file ota_service.h
 * @brief Dual-channel OTA (HTTP + SD) with user confirmation.
 *
 * Design (docs/PLAN.md section 8):
 *  - HTTP channel: follows GitHub Releases, strictly new > current.
 *  - SD channel:    force mode (downgrade allowed), one final confirmation.
 *  - Download writes the INACTIVE OTA slot directly; switching + reboot only
 *    after ota_service_confirm().
 *  - Single-instance state machine: concurrent triggers are rejected.
 */

#ifndef OTA_SERVICE_H
#define OTA_SERVICE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OTA_VERSION_MAX      16U
#define OTA_EVENT_INFO_MAX   128U

typedef enum {
    OTA_STATE_IDLE = 0,
    OTA_STATE_CHECKING,
    OTA_STATE_DOWNLOADING,
    OTA_STATE_VERIFYING,
    OTA_STATE_PENDING_APPLY,
    OTA_STATE_SWITCHING,
    OTA_STATE_REBOOTING,
    OTA_STATE_FAILED,
} ota_state_t;

typedef enum {
    OTA_CHANNEL_HTTP = 0,
    OTA_CHANNEL_SD,
} ota_channel_t;

typedef enum {
    OTA_RESULT_NONE = 0,
    OTA_RESULT_OK,
    OTA_RESULT_FAILED,
    OTA_RESULT_ROLLED_BACK,
    OTA_RESULT_CANCELLED,
} ota_result_t;

typedef struct {
    char version[OTA_VERSION_MAX];      /*< 目标版本 */
    ota_channel_t channel;              /*< 触发通道 */
    uint8_t progress_pct;               /*< 0..100（下载/校验阶段） */
} ota_event_info_t;

typedef struct {
    ota_state_t state;
    char running_version[OTA_VERSION_MAX];
    char running_label[17];
    char next_label[17];
    bool pending;
    char pending_version[OTA_VERSION_MAX];
    ota_result_t last_result;
    char last_result_detail[OTA_EVENT_INFO_MAX];
} ota_status_t;

typedef void (*ota_event_cb_t)(ota_state_t state, const ota_event_info_t *info);

/**
 * @brief Initialize the OTA service (read NVS state, prepare timers).
 * @param[in] cb State-change callback (may be NULL).
 * @return ESP_OK on success.
 */
esp_err_t ota_service_init(ota_event_cb_t cb);

/**
 * @brief Check + download + verify via HTTP (new > current only).
 *        Ends in OTA_STATE_PENDING_APPLY and starts the confirm timeout.
 * @return ESP_OK when the flow started, ESP_ERR_INVALID_STATE when busy.
 */
esp_err_t ota_service_check_http(void);

/**
 * @brief Force upgrade from the SD card (downgrade allowed).
 *        Mounts the card, scans the update dir, downloads into the inactive
 *        slot, then ends in OTA_STATE_PENDING_APPLY.
 * @return ESP_OK when the flow started.
 */
esp_err_t ota_service_apply_sd(void);

/**
 * @brief User confirmation: switch boot partition + 3 s countdown + reboot.
 * @return ESP_OK on success (never returns after reboot).
 */
esp_err_t ota_service_confirm(void);

/**
 * @brief Cancel a pending update (keeps the current firmware running).
 * @return ESP_OK on success.
 */
esp_err_t ota_service_cancel(void);

/**
 * @brief Get the current OTA state.
 * @return Current state.
 */
ota_state_t ota_service_get_state(void);

/**
 * @brief Get a human readable name for an OTA state.
 * @param[in] state State value.
 * @return NUL terminated string.
 */
const char *ota_service_get_state_name(ota_state_t state);

/**
 * @brief Get a status snapshot (for diag console / snapshot / UI).
 * @param[out] status Filled status.
 * @return ESP_OK on success.
 */
esp_err_t ota_service_get_status(ota_status_t *status);

#ifdef __cplusplus
}
#endif

#endif /* OTA_SERVICE_H */


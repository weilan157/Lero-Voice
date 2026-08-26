/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file ota_service.c
 * @brief OTA orchestration: single-instance state machine (docs/PLAN.md 8.8).
 *
 *   IDLE -> CHECKING -> DOWNLOADING -> VERIFYING -> PENDING_APPLY
 *        -> (confirm) SWITCHING -> REBOOTING -> reboot
 *        -> (cancel / timeout) IDLE (pending kept in NVS)
 */

#include <string.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "ota_internal.h"

#define TAG "ota_service"

static ota_state_t s_state;
static ota_event_cb_t s_cb;
static char s_target_version[OTA_VERSION_MAX];
static ota_channel_t s_channel;
static uint8_t s_progress;
static esp_timer_handle_t s_confirm_timer;
static esp_timer_handle_t s_health_timer;
static StaticSemaphore_t s_mutex_tcb;
static SemaphoreHandle_t s_mutex;

const char *ota_state_name(ota_state_t state)
{
    static const char *const s_names[] = {
        "IDLE", "CHECKING", "DOWNLOADING", "VERIFYING",
        "PENDING_APPLY", "SWITCHING", "REBOOTING", "FAILED",
    };
    const uint32_t idx = (uint32_t)state;
    return (idx < (sizeof(s_names) / sizeof(s_names[0]))) ? s_names[idx] : "?";
}

void ota_notify(ota_state_t state, const char *version, uint8_t pct)
{
    if (s_cb == NULL) {
        return;
    }
    ota_event_info_t info;
    (void)memset(&info, 0, sizeof(info));
    (void)strlcpy(info.version, (version != NULL) ? version : "-", sizeof(info.version));
    info.channel = s_channel;
    info.progress_pct = pct;
    s_cb(state, &info);
}

void ota_set_state(ota_state_t state, const char *version, uint8_t pct)
{
    s_state = state;
    s_progress = pct;
    if (version != NULL) {
        (void)strlcpy(s_target_version, version, sizeof(s_target_version));
    }
    ESP_LOGI(TAG, "state=%s version=%s pct=%u",
             ota_state_name(state), s_target_version, (unsigned)pct);
    ota_notify(state, s_target_version, pct);
}

static void s_progress_cb(uint8_t pct, void *ctx)
{
    (void)ctx;
    ota_notify(s_state, s_target_version, pct);
}

static void s_confirm_timeout_cb(void *arg)
{
    (void)arg;
    ESP_LOGW(TAG, "confirm timeout: keep pending, back to idle ('later')");
    ota_set_state(OTA_STATE_IDLE, s_target_version, s_progress);
}

/* 升级后健康自检窗口结束：标记新固件有效，取消回滚（PLAN 8.4 step 7）。
 * 版本自证在 ota_partition_boot_state() 中完成；自证失败不启动本定时器。 */
static void s_health_check_cb(void *arg)
{
    (void)arg;
    esp_err_t err = ota_partition_mark_valid();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "health check passed, new image confirmed");
    } else {
        ESP_LOGE(TAG, "mark valid failed: %s", esp_err_to_name(err));
    }
}

static void s_start_confirm_timer(void)
{
    if (s_confirm_timer != NULL) {
        (void)esp_timer_stop(s_confirm_timer);
        (void)esp_timer_start_once(s_confirm_timer,
                                   (uint64_t)CONFIG_LERO_OTA_CONFIRM_TIMEOUT_MS * 1000U);
    }
}

static void s_stop_confirm_timer(void)
{
    if (s_confirm_timer != NULL) {
        (void)esp_timer_stop(s_confirm_timer);
    }
}

static esp_err_t s_fail(const char *detail)
{
    (void)ota_state_set_result(OTA_RESULT_FAILED, detail);
    ota_set_state(OTA_STATE_FAILED, s_target_version, s_progress);
    return ESP_OK;                      /* 保持 FAILED 可见，直到下次操作/重试 */
}


/* ------------------------------------------------------------------------- */
/* Flow implementations                                                      */
/* ------------------------------------------------------------------------- */

esp_err_t ota_service_check_http(void)
{
    if ((s_mutex == NULL) || (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE)) {
        return ESP_ERR_INVALID_STATE;
    }
    if ((s_state != OTA_STATE_IDLE) && (s_state != OTA_STATE_PENDING_APPLY) &&
        (s_state != OTA_STATE_FAILED)) {
        (void)xSemaphoreGive(s_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    ota_set_state(OTA_STATE_CHECKING, NULL, 0U);

    ota_meta_t meta;
    esp_err_t err = ota_http_fetch_meta(CONFIG_LERO_OTA_HTTP_META_URL, &meta);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "meta fetch failed: %s", esp_err_to_name(err));
        (void)s_fail("meta fetch failed");
        (void)xSemaphoreGive(s_mutex);
        return ESP_OK;
    }

    /* pending 同版本：跳过重复下载，直接进入询问（8.4 step 9） */
    bool pending = false;
    char pending_version[OTA_VERSION_MAX];
    ota_channel_t pending_channel = OTA_CHANNEL_HTTP;
    (void)ota_state_get_pending(&pending, pending_version, sizeof(pending_version),
                                &pending_channel);
    if (pending && (strcmp(pending_version, meta.version) == 0)) {
        ESP_LOGI(TAG, "pending %s already downloaded, ask again", meta.version);
        s_channel = pending_channel;
        ota_set_state(OTA_STATE_PENDING_APPLY, meta.version, 100U);
        s_start_confirm_timer();
        (void)xSemaphoreGive(s_mutex);
        return ESP_OK;
    }

    bool is_newer = false;
    err = ota_meta_validate(&meta, &is_newer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "meta invalid: %s", esp_err_to_name(err));
        (void)s_fail("meta invalid");
        (void)xSemaphoreGive(s_mutex);
        return ESP_OK;
    }
    if (!is_newer) {
        ESP_LOGI(TAG, "no newer version (latest %s)", meta.version);
        ota_set_state(OTA_STATE_IDLE, meta.version, 0U);
        (void)xSemaphoreGive(s_mutex);
        return ESP_OK;
    }

    bool battery_ok = false;
    (void)ota_state_battery_gate(&battery_ok);
    if (!battery_ok) {
        (void)s_fail("battery too low");
        (void)xSemaphoreGive(s_mutex);
        return ESP_OK;
    }

    const esp_partition_t *next = NULL;
    err = ota_partition_get_next(&next);
    if (err != ESP_OK) {
        (void)s_fail("no OTA slot");
        (void)xSemaphoreGive(s_mutex);
        return ESP_OK;
    }

    s_channel = OTA_CHANNEL_HTTP;
    ota_set_state(OTA_STATE_DOWNLOADING, meta.version, 0U);
    err = ota_http_download(CONFIG_LERO_OTA_HTTP_META_URL, next, &meta,
                            s_progress_cb, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "download/verify failed: %s", esp_err_to_name(err));
        (void)s_fail("download/verify failed");
        (void)xSemaphoreGive(s_mutex);
        return ESP_OK;
    }

    ota_set_state(OTA_STATE_VERIFYING, meta.version, 100U);
    (void)ota_state_incr_attempts();
    (void)ota_state_set_pending(&meta, OTA_CHANNEL_HTTP);
    ota_set_state(OTA_STATE_PENDING_APPLY, meta.version, 100U);
    s_start_confirm_timer();

    (void)xSemaphoreGive(s_mutex);
    return ESP_OK;
}

esp_err_t ota_service_apply_sd(void)
{
    if ((s_mutex == NULL) || (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE)) {
        return ESP_ERR_INVALID_STATE;
    }
    if ((s_state != OTA_STATE_IDLE) && (s_state != OTA_STATE_PENDING_APPLY) &&
        (s_state != OTA_STATE_FAILED)) {
        (void)xSemaphoreGive(s_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    ota_set_state(OTA_STATE_CHECKING, "sd", 0U);

    ota_meta_t meta;
    esp_err_t err = ota_sd_find_update(&meta);
    if (err != ESP_OK) {
        (void)s_fail("no SD update");
        (void)xSemaphoreGive(s_mutex);
        return ESP_OK;
    }

    bool battery_ok = false;
    (void)ota_state_battery_gate(&battery_ok);
    if (!battery_ok) {
        (void)s_fail("battery too low");
        (void)xSemaphoreGive(s_mutex);
        return ESP_OK;
    }

    const esp_partition_t *next = NULL;
    err = ota_partition_get_next(&next);
    if (err != ESP_OK) {
        (void)s_fail("no OTA slot");
        (void)xSemaphoreGive(s_mutex);
        return ESP_OK;
    }

    s_channel = OTA_CHANNEL_SD;
    ota_set_state(OTA_STATE_DOWNLOADING, meta.version, 0U);
    err = ota_sd_write_partition(next, &meta, s_progress_cb, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SD write/verify failed: %s", esp_err_to_name(err));
        (void)s_fail("SD write/verify failed");
        (void)xSemaphoreGive(s_mutex);
        return ESP_OK;
    }

    ota_set_state(OTA_STATE_VERIFYING, meta.version, 100U);
    (void)ota_state_incr_attempts();
    (void)ota_state_set_pending(&meta, OTA_CHANNEL_SD);
    ota_set_state(OTA_STATE_PENDING_APPLY, meta.version, 100U);
    s_start_confirm_timer();

    (void)xSemaphoreGive(s_mutex);
    return ESP_OK;
}

esp_err_t ota_service_confirm(void)
{
    if ((s_mutex == NULL) || (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_state != OTA_STATE_PENDING_APPLY) {
        ESP_LOGW(TAG, "confirm ignored: state=%s", ota_state_name(s_state));
        (void)xSemaphoreGive(s_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    s_stop_confirm_timer();

    bool battery_ok = false;
    (void)ota_state_battery_gate(&battery_ok);
    if (!battery_ok) {
        (void)s_fail("battery too low at confirm");
        (void)xSemaphoreGive(s_mutex);
        return ESP_OK;
    }

    ota_set_state(OTA_STATE_SWITCHING, s_target_version, 100U);
    esp_err_t err = ota_partition_set_boot_to_next();
    if (err != ESP_OK) {
        (void)s_fail("set boot partition failed");
        (void)xSemaphoreGive(s_mutex);
        return err;
    }
    (void)ota_state_reset_attempts();
    (void)ota_state_set_result(OTA_RESULT_OK, "confirmed");

    ota_set_state(OTA_STATE_REBOOTING, s_target_version, 100U);
    (void)xSemaphoreGive(s_mutex);
    vTaskDelay(pdMS_TO_TICKS(CONFIG_LERO_OTA_REBOOT_COUNTDOWN_MS));
    esp_restart();
    return ESP_OK;                      /* unreachable */
}

esp_err_t ota_service_cancel(void)
{
    if ((s_mutex == NULL) || (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE)) {
        return ESP_ERR_INVALID_STATE;
    }
    s_stop_confirm_timer();
    (void)ota_state_clear_pending();
    (void)ota_state_set_result(OTA_RESULT_CANCELLED, "cancelled");
    ota_set_state(OTA_STATE_IDLE, s_target_version, 0U);
    (void)xSemaphoreGive(s_mutex);
    return ESP_OK;
}

/* ------------------------------------------------------------------------- */
/* Public accessors                                                          */
/* ------------------------------------------------------------------------- */

esp_err_t ota_service_init(ota_event_cb_t cb)
{
    s_cb = cb;
    s_mutex = xSemaphoreCreateMutexStatic(&s_mutex_tcb);
    if (s_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    (void)ota_state_init();

    esp_timer_create_args_t timer_args = {
        .callback = s_confirm_timeout_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "ota_confirm",
        .skip_unhandled_events = false,
    };
    esp_err_t err = esp_timer_create(&timer_args, &s_confirm_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "confirm timer create failed: %s", esp_err_to_name(err));
        return err;
    }

    /* 升级后回滚确认（PLAN 8.4 step 7 / 8.8）：
     * 新固件首次启动若处于 PENDING_VERIFY 且版本自证通过，
     * 在健康窗口后标记有效；自证失败则交由 bootloader 自动回滚。 */
    bool pending_verify = false;
    bool healthy = false;
    if (ota_partition_boot_state(&pending_verify, &healthy) == ESP_OK) {
        if (pending_verify && healthy) {
            esp_timer_create_args_t health_args = {
                .callback = s_health_check_cb,
                .arg = NULL,
                .dispatch_method = ESP_TIMER_TASK,
                .name = "ota_health",
                .skip_unhandled_events = false,
            };
            if (esp_timer_create(&health_args, &s_health_timer) == ESP_OK) {
                (void)esp_timer_start_once(
                    s_health_timer,
                    (uint64_t)CONFIG_LERO_OTA_HEALTH_CHECK_MS * 1000U);
                ESP_LOGI(TAG, "boot health check scheduled in %u ms",
                         (unsigned)CONFIG_LERO_OTA_HEALTH_CHECK_MS);
            }
        } else if (pending_verify) {
            ESP_LOGE(TAG, "version self-check failed; not marking valid "
                          "(bootloader will roll back)");
        }
    }

    (void)ota_partition_get_running_desc(s_target_version, sizeof(s_target_version),
                                         NULL, 0U);
    bool pending = false;
    char version[OTA_VERSION_MAX];
    ota_channel_t channel = OTA_CHANNEL_HTTP;
    (void)ota_state_get_pending(&pending, version, sizeof(version), &channel);
    if (pending) {
        ESP_LOGI(TAG, "pending update %s found at boot (channel %d)",
                 version, (int)channel);
    }
    ota_set_state(OTA_STATE_IDLE, s_target_version, 0U);
    return ESP_OK;
}

ota_state_t ota_service_get_state(void)
{
    return s_state;
}

const char *ota_service_get_state_name(ota_state_t state)
{
    return ota_state_name(state);
}

esp_err_t ota_service_get_status(ota_status_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    (void)memset(status, 0, sizeof(*status));
    status->state = s_state;
    (void)strlcpy(status->running_version, s_target_version, sizeof(status->running_version));
    (void)ota_partition_get_running_desc(status->running_version,
                                         sizeof(status->running_version),
                                         status->running_label, sizeof(status->running_label));
    (void)ota_partition_get_next_desc(status->next_label, sizeof(status->next_label));

    bool pending = false;
    char version[OTA_VERSION_MAX];
    ota_channel_t channel = OTA_CHANNEL_HTTP;
    (void)ota_state_get_pending(&pending, version, sizeof(version), &channel);
    status->pending = pending;
    (void)strlcpy(status->pending_version, version, sizeof(status->pending_version));

    (void)ota_state_get_result(&status->last_result, status->last_result_detail,
                               sizeof(status->last_result_detail));
    return ESP_OK;
}

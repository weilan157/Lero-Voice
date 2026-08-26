/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file diag_errors.c
 * @brief Fault bitmap + crash counter + reset reason (NVS "diag").
 *
 * Persisted values are written only on change (docs/PLAN.md 8.11 #9).
 */

#include <string.h>
#include "esp_log.h"
#include "esp_system.h"
#include "nvs.h"
#include "diag_internal.h"

#define TAG "diag_errors"

#define NVS_NS_DIAG     "diag"
#define KEY_FAULTS      "faults"
#define KEY_CRASHES     "crashes"

static uint32_t s_faults;
static uint32_t s_crashes;
static esp_reset_reason_t s_reset_reason;

static esp_err_t s_nvs_get_u32(const char *key, uint32_t *val)
{
    nvs_handle_t h = 0U;
    esp_err_t err = nvs_open(NVS_NS_DIAG, NVS_READONLY, &h);
    if (err != ESP_OK) {
        *val = 0U;
        return err;
    }
    err = nvs_get_u32(h, key, val);
    (void)nvs_close(h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *val = 0U;
        return ESP_OK;
    }
    return err;
}

static esp_err_t s_nvs_set_u32(const char *key, uint32_t val)
{
    nvs_handle_t h = 0U;
    esp_err_t err = nvs_open(NVS_NS_DIAG, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_u32(h, key, val);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    (void)nvs_close(h);
    return err;
}

esp_err_t diag_errors_init(void)
{
    s_reset_reason = esp_reset_reason();
    (void)s_nvs_get_u32(KEY_FAULTS, &s_faults);
    (void)s_nvs_get_u32(KEY_CRASHES, &s_crashes);
    if (s_reset_reason == ESP_RST_PANIC) {
        s_crashes++;
        (void)s_nvs_set_u32(KEY_CRASHES, s_crashes);
        ESP_LOGW(TAG, "boot after panic (crash count %u)", (unsigned)s_crashes);
    }
    ESP_LOGI(TAG, "reset reason=%s faults=0x%08lx crashes=%lu",
             diag_errors_get_reset_reason_str(),
             (unsigned long)s_faults, (unsigned long)s_crashes);
    return ESP_OK;
}

void diag_errors_update_faults(uint32_t faults)
{
    if (faults != s_faults) {
        s_faults = faults;
        (void)s_nvs_set_u32(KEY_FAULTS, s_faults);
    }
}

uint32_t diag_errors_get_faults(void)
{
    return s_faults;
}

uint32_t diag_errors_get_crash_count(void)
{
    return s_crashes;
}

const char *diag_errors_get_reset_reason_str(void)
{
    switch (s_reset_reason) {
    case ESP_RST_POWERON:    return "power-on";
    case ESP_RST_SW:         return "software";
    case ESP_RST_PANIC:      return "panic";
    case ESP_RST_INT_WDT:    return "interrupt-wdt";
    case ESP_RST_TASK_WDT:   return "task-wdt";
    case ESP_RST_WDT:        return "other-wdt";
    case ESP_RST_DEEPSLEEP:  return "deep-sleep";
    case ESP_RST_BROWNOUT:   return "brownout";
    case ESP_RST_SDIO:       return "sdio";
    case ESP_RST_USB:        return "usb";
    case ESP_RST_JTAG:       return "jtag";
    case ESP_RST_EFUSE:      return "efuse";
    case ESP_RST_PWR_GLITCH: return "power-glitch";
    case ESP_RST_CPU_LOCKUP: return "cpu-lockup";
    default:                 return "unknown";
    }
}


/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file ota_state.c
 * @brief OTA state persistence (NVS namespace "ota") + battery gate.
 *
 * Keys are written only on change to limit NVS wear (docs/PLAN.md 8.11 #9).
 * The battery gate refuses download/switch below the configured level unless
 * the charger is attached (PLAN 8.2 / 8.9).
 */

#include <string.h>
#include "esp_log.h"
#include "nvs.h"
#include "bsp_power.h"
#include "ota_internal.h"

#define TAG "ota_state"

#define NVS_NS_OTA        "ota"
#define KEY_PENDING       "pending"
#define KEY_VERSION       "version"
#define KEY_CHANNEL       "channel"
#define KEY_RESULT        "result"
#define KEY_DETAIL        "detail"
#define KEY_ATTEMPTS      "attempts"

esp_err_t ota_state_init(void)
{
    return ESP_OK;
}

static esp_err_t s_open(nvs_handle_t *h, nvs_open_mode_t mode)
{
    return nvs_open(NVS_NS_OTA, mode, h);
}

esp_err_t ota_state_set_pending(const ota_meta_t *meta, ota_channel_t channel)
{
    if (meta == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t h = 0U;
    esp_err_t err = s_open(&h, NVS_READWRITE);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_u8(h, KEY_PENDING, 1U);
    if (err == ESP_OK) {
        err = nvs_set_str(h, KEY_VERSION, meta->version);
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(h, KEY_CHANNEL, (uint8_t)channel);
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    (void)nvs_close(h);
    return err;
}

esp_err_t ota_state_clear_pending(void)
{
    nvs_handle_t h = 0U;
    esp_err_t err = s_open(&h, NVS_READWRITE);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_u8(h, KEY_PENDING, 0U);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    (void)nvs_close(h);
    return err;
}

esp_err_t ota_state_get_pending(bool *pending, char *version, size_t vlen,
                                ota_channel_t *channel)
{
    if ((pending == NULL) || (version == NULL) || (channel == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    *pending = false;
    version[0] = '\0';
    *channel = OTA_CHANNEL_HTTP;

    nvs_handle_t h = 0U;
    esp_err_t err = s_open(&h, NVS_READONLY);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }
    uint8_t pend = 0U;
    uint8_t ch = 0U;
    err = nvs_get_u8(h, KEY_PENDING, &pend);
    if (err == ESP_OK) {
        (void)nvs_get_u8(h, KEY_CHANNEL, &ch);
        size_t len = vlen;
        if (nvs_get_str(h, KEY_VERSION, version, &len) != ESP_OK) {
            version[0] = '\0';
        }
        *pending = (pend != 0U);
        *channel = (ota_channel_t)ch;
    }
    (void)nvs_close(h);
    return ESP_OK;
}

esp_err_t ota_state_set_result(ota_result_t result, const char *detail)
{
    nvs_handle_t h = 0U;
    esp_err_t err = s_open(&h, NVS_READWRITE);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_u8(h, KEY_RESULT, (uint8_t)result);
    if ((err == ESP_OK) && (detail != NULL)) {
        err = nvs_set_str(h, KEY_DETAIL, detail);
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    (void)nvs_close(h);
    return err;
}

esp_err_t ota_state_get_result(ota_result_t *result, char *detail, size_t dlen)
{
    if ((result == NULL) || (detail == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    *result = OTA_RESULT_NONE;
    detail[0] = '\0';
    nvs_handle_t h = 0U;
    esp_err_t err = s_open(&h, NVS_READONLY);
    if (err != ESP_OK) {
        return ESP_OK;
    }
    uint8_t r = 0U;
    if (nvs_get_u8(h, KEY_RESULT, &r) == ESP_OK) {
        *result = (ota_result_t)r;
        size_t len = dlen;
        if (nvs_get_str(h, KEY_DETAIL, detail, &len) != ESP_OK) {
            detail[0] = '\0';
        }
    }
    (void)nvs_close(h);
    return ESP_OK;
}

esp_err_t ota_state_incr_attempts(void)
{
    nvs_handle_t h = 0U;
    esp_err_t err = s_open(&h, NVS_READWRITE);
    if (err != ESP_OK) {
        return err;
    }
    uint8_t attempts = 0U;
    (void)nvs_get_u8(h, KEY_ATTEMPTS, &attempts);
    if (attempts < 250U) {
        attempts++;
    }
    err = nvs_set_u8(h, KEY_ATTEMPTS, attempts);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    (void)nvs_close(h);
    return err;
}

esp_err_t ota_state_reset_attempts(void)
{
    nvs_handle_t h = 0U;
    esp_err_t err = s_open(&h, NVS_READWRITE);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_u8(h, KEY_ATTEMPTS, 0U);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    (void)nvs_close(h);
    return err;
}

esp_err_t ota_state_battery_gate(bool *ok)
{
    if (ok == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *ok = true;
    uint8_t pct = 0U;
    bool charging = false;
    esp_err_t err = bsp_power_get_battery_pct(&pct);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "battery level unavailable, gate open");
        return ESP_OK;
    }
    (void)bsp_power_get_charge_state(&charging);
    if (!charging && (pct < (uint8_t)CONFIG_LERO_OTA_BATTERY_THRESHOLD_PCT)) {
        ESP_LOGW(TAG, "battery %u%% below %d%% (charging=%d) - refuse OTA",
                 (unsigned)pct, CONFIG_LERO_OTA_BATTERY_THRESHOLD_PCT, (int)charging);
        *ok = false;
    }
    return ESP_OK;
}


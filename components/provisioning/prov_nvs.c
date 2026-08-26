/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file prov_nvs.c
 * @brief WiFi credential persistence (NVS namespace "wifi").
 *
 * Keys: ssid / password / configured. Factory reset erases the whole NVS and
 * formats the storage partition (docs/PLAN.md 4.3 step 5 / 8.6 #6).
 */

#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_system.h"
#include "bsp_storage.h"
#include "prov_internal.h"

#define TAG "prov_nvs"

#define NVS_NS_WIFI         "wifi"
#define NVS_KEY_SSID        "ssid"
#define NVS_KEY_PASSWORD    "password"
#define NVS_KEY_CONFIGURED  "configured"

esp_err_t prov_nvs_load_wifi(char *ssid, size_t ssid_len, char *pwd, size_t pwd_len,
                             bool *configured)
{
    if ((ssid == NULL) || (pwd == NULL) || (configured == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    *configured = false;
    nvs_handle_t h = 0U;
    esp_err_t err = nvs_open(NVS_NS_WIFI, NVS_READONLY, &h);
    if (err != ESP_OK) {
        return err;
    }
    uint8_t cfg = 0U;
    err = nvs_get_u8(h, NVS_KEY_CONFIGURED, &cfg);
    if ((err != ESP_OK) || (cfg == 0U)) {
        (void)nvs_close(h);
        return ESP_OK;
    }
    size_t len = ssid_len;
    err = nvs_get_str(h, NVS_KEY_SSID, ssid, &len);
    if (err != ESP_OK) {
        (void)nvs_close(h);
        return err;
    }
    len = pwd_len;
    err = nvs_get_str(h, NVS_KEY_PASSWORD, pwd, &len);
    (void)nvs_close(h);
    if (err == ESP_OK) {
        *configured = true;
    }
    return err;
}

esp_err_t prov_nvs_save_wifi(const char *ssid, const char *pwd)
{
    if ((ssid == NULL) || (pwd == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t h = 0U;
    esp_err_t err = nvs_open(NVS_NS_WIFI, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(h, NVS_KEY_SSID, ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(h, NVS_KEY_PASSWORD, pwd);
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(h, NVS_KEY_CONFIGURED, 1U);
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    (void)nvs_close(h);
    return err;
}

esp_err_t prov_nvs_clear_wifi(void)
{
    nvs_handle_t h = 0U;
    esp_err_t err = nvs_open(NVS_NS_WIFI, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_erase_all(h);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    (void)nvs_close(h);
    return err;
}

esp_err_t prov_nvs_factory_reset(void)
{
    ESP_LOGW(TAG, "factory reset: erasing NVS + storage");
    (void)bsp_storage_unmount();
    const esp_err_t fmt_err = bsp_storage_format();
    if (fmt_err != ESP_OK) {
        ESP_LOGW(TAG, "storage format failed: %s", esp_err_to_name(fmt_err));
    }
    esp_err_t err = nvs_flash_erase();
    if (err == ESP_OK) {
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "factory reset failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGW(TAG, "NVS erased; rebooting into provisioning");
    esp_restart();
    return ESP_OK;   /* unreachable on success */
}


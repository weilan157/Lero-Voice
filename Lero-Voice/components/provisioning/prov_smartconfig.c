/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file prov_smartconfig.c
 * @brief SmartConfig (ESP-TOUCH v2) listener wrapper.
 *
 * The SC_EVENT handler lives in provisioning.c; this module only owns the
 * start/stop lifecycle and the temporary credentials buffer.
 */

#include <string.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_smartconfig.h"
#include "prov_internal.h"

#define TAG "prov_sc"

static bool s_running;
static char s_ssid[PROV_SSID_MAX + 1U];
static char s_pwd[PROV_PWD_MAX + 1U];
static bool s_creds_valid;

esp_err_t prov_smartconfig_start(void)
{
    if (s_running) {
        return ESP_OK;
    }
    esp_err_t err = esp_smartconfig_set_type(SC_TYPE_ESPTOUCH_V2);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set type failed: %s", esp_err_to_name(err));
        return err;
    }
    smartconfig_start_config_t cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
    err = esp_smartconfig_start(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "start failed: %s", esp_err_to_name(err));
        return err;
    }
    s_running = true;
    ESP_LOGI(TAG, "smartconfig listening (ESP-TOUCH v2, %d s)",
             CONFIG_LERO_PROV_SMARTCONFIG_TIMEOUT_MS / 1000);
    return ESP_OK;
}

esp_err_t prov_smartconfig_stop(void)
{
    if (!s_running) {
        return ESP_OK;
    }
    esp_err_t err = esp_smartconfig_stop();
    s_running = (err != ESP_OK);
    return err;
}

bool prov_smartconfig_running(void)
{
    return s_running;
}

void prov_smartconfig_set_creds(const char *ssid, const char *pwd)
{
    if ((ssid == NULL) || (pwd == NULL)) {
        return;
    }
    (void)strlcpy(s_ssid, ssid, sizeof(s_ssid));
    (void)strlcpy(s_pwd, pwd, sizeof(s_pwd));
    s_creds_valid = true;
}

esp_err_t prov_smartconfig_get_creds(char *ssid, size_t ssid_len,
                                     char *pwd, size_t pwd_len)
{
    if ((ssid == NULL) || (pwd == NULL) || !s_creds_valid) {
        return ESP_ERR_INVALID_STATE;
    }
    (void)strlcpy(ssid, s_ssid, ssid_len);
    (void)strlcpy(pwd, s_pwd, pwd_len);
    return ESP_OK;
}


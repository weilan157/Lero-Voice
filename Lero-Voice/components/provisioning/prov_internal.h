/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file prov_internal.h
 * @brief Internal interface shared by the provisioning modules.
 */

#ifndef PROV_INTERNAL_H
#define PROV_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "provisioning.h"
#include "esp_wifi.h"

#define PROV_SSID_MAX   32U
#define PROV_PWD_MAX    64U

/* provisioning.c */
void prov_set_state(prov_state_t state, const char *detail);
void prov_notify(const char *detail);
esp_err_t prov_wifi_set_mode_start(wifi_mode_t mode);
esp_err_t prov_wifi_connect_sta(const char *ssid, const char *pwd);
esp_err_t prov_wifi_start_sta(void);
esp_err_t prov_wifi_disconnect(void);
bool prov_wifi_is_connected(void);
esp_err_t prov_get_sta_ip(char *ip, size_t ip_len);
esp_err_t prov_get_sta_rssi(int8_t *rssi);
bool prov_is_provision_session(void);
void prov_set_provision_session(bool on);
esp_err_t prov_probe_network(void);

/* prov_nvs.c */
esp_err_t prov_nvs_load_wifi(char *ssid, size_t ssid_len, char *pwd, size_t pwd_len,
                             bool *configured);
esp_err_t prov_nvs_save_wifi(const char *ssid, const char *pwd);
esp_err_t prov_nvs_clear_wifi(void);
esp_err_t prov_nvs_factory_reset(void);

/* prov_smartconfig.c */
esp_err_t prov_smartconfig_start(void);
esp_err_t prov_smartconfig_stop(void);
bool prov_smartconfig_running(void);
esp_err_t prov_smartconfig_get_creds(char *ssid, size_t ssid_len,
                                     char *pwd, size_t pwd_len);
void prov_smartconfig_set_creds(const char *ssid, const char *pwd);

/* prov_softap.c */
esp_err_t prov_softap_start(void);
esp_err_t prov_softap_stop(void);
bool prov_softap_running(void);

#endif /* PROV_INTERNAL_H */


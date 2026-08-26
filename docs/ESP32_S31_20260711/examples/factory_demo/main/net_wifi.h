/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NET_WIFI_SSID_MAX_LEN 33
#define NET_WIFI_SCAN_MAX     16

typedef enum {
    NET_WIFI_IDLE = 0,
    NET_WIFI_SCANNING,
    NET_WIFI_CONNECTING,
    NET_WIFI_CONNECTED,
    NET_WIFI_FAILED,
} net_wifi_state_t;

typedef struct {
    char ssid[NET_WIFI_SSID_MAX_LEN];
    int8_t rssi;    /*!< signal strength in dBm */
    bool locked;    /*!< true if the AP is encrypted */
} net_wifi_ap_t;

/** Initialize NVS, netif, event loop and start Wi-Fi in station mode. */
esp_err_t net_wifi_init(void);

/** Kick off a non-blocking AP scan. Results retrieved with net_wifi_scan_get(). */
void net_wifi_scan_start(void);

/** True while a scan is in progress. */
bool net_wifi_scan_busy(void);

/** Copy up to max_count scanned SSIDs into ssids; returns the number copied. */
int net_wifi_scan_get(char ssids[][NET_WIFI_SSID_MAX_LEN], int max_count);

/** Copy up to max_count scanned APs (ssid + rssi + locked); returns the number copied. */
int net_wifi_scan_get_aps(net_wifi_ap_t *aps, int max_count);

/** Connect to the given AP; optionally persist credentials to NVS. */
esp_err_t net_wifi_connect(const char *ssid, const char *password, bool save);

/** Current connection state. */
net_wifi_state_t net_wifi_get_state(void);

/** Copy the acquired IPv4 address string; returns false if not connected. */
bool net_wifi_get_ip(char *out, size_t len);

/** Load previously saved credentials from NVS; returns false if none. */
bool net_wifi_load_creds(char *ssid, size_t ssid_len, char *pass, size_t pass_len);

#ifdef __cplusplus
}
#endif

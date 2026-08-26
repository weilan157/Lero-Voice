/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file diag_snapshot.c
 * @brief Periodic status snapshots (1 s, docs/PLAN.md 3.8.2).
 *
 * Ring of fixed-size text records consumed by the diagnostics page / the
 * "snapshot" console command. Sensitive values are masked by the producers
 * (provisioning masks credentials; we never log passwords).
 */

#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "bsp.h"
#include "bsp_power.h"
#include "bsp_sdcard.h"
#include "provisioning.h"
#include "ota_service.h"
#include "diag_internal.h"

#define TAG "diag_snapshot"

#define SNAP_LINE_MAX   256U

static char s_slots[CONFIG_LERO_DIAG_SNAPSHOT_SLOTS][SNAP_LINE_MAX];
static uint8_t s_head;
static uint8_t s_count;

esp_err_t diag_snapshot_init(void)
{
    s_head = 0U;
    s_count = 0U;
    return ESP_OK;
}

static void s_append_slot(const char *line)
{
    (void)strlcpy(s_slots[s_head], line, SNAP_LINE_MAX);
    s_head = (uint8_t)((s_head + 1U) % (uint8_t)CONFIG_LERO_DIAG_SNAPSHOT_SLOTS);
    if (s_count < (uint8_t)CONFIG_LERO_DIAG_SNAPSHOT_SLOTS) {
        s_count++;
    }
}

esp_err_t diag_snapshot_capture(void)
{
    char line[SNAP_LINE_MAX];
    const uint32_t uptime_s = (uint32_t)(esp_timer_get_time() / 1000000U);

    uint32_t bat_mv = 0U;
    uint8_t bat_pct = 0U;
    bool charging = false;
    if (bsp_power_get_battery_mv(&bat_mv) != ESP_OK) {
        bat_mv = 0U;
    }
    (void)bsp_power_get_battery_pct(&bat_pct);
    (void)bsp_power_get_charge_state(&charging);

    prov_wifi_status_t wifi;
    (void)memset(&wifi, 0, sizeof(wifi));
    (void)prov_get_wifi_status(&wifi);

    ota_status_t ota;
    (void)memset(&ota, 0, sizeof(ota));
    (void)ota_service_get_status(&ota);

    (void)snprintf(line, sizeof(line),
                   "{\"uptime_s\":%lu,\"heap\":%lu,\"bat_mv\":%lu,\"bat_pct\":%u,"
                   "\"chg\":%d,\"rssi\":%d,\"ip\":\"%s\",\"faults\":\"0x%08lx\","
                   "\"ota\":\"%s\",\"pending\":%d,\"rst\":\"%s\"}",
                   (unsigned long)uptime_s,
                   (unsigned long)esp_get_free_heap_size(),
                   (unsigned long)bat_mv, (unsigned)bat_pct, (int)charging,
                   (int)wifi.rssi, wifi.ip,
                   (unsigned long)bsp_get_fault_bitmap(),
                   ota_service_get_state_name(ota.state), (int)ota.pending,
                   diag_errors_get_reset_reason_str());
    s_append_slot(line);
    return ESP_OK;
}

esp_err_t diag_snapshot_get_latest(char *out, size_t len)
{
    if ((out == NULL) || (len == 0U)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_count == 0U) {
        (void)strlcpy(out, "(no snapshot yet)", len);
        return ESP_OK;
    }
    const uint8_t last = (uint8_t)((s_head + (uint8_t)CONFIG_LERO_DIAG_SNAPSHOT_SLOTS - 1U) %
                                   (uint8_t)CONFIG_LERO_DIAG_SNAPSHOT_SLOTS);
    (void)strlcpy(out, s_slots[last], len);
    return ESP_OK;
}


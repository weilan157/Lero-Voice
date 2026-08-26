/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool valid;
    char cond[48];      /*!< Chinese weather condition, e.g. "晴" */
    char temp[16];      /*!< e.g. "+18°C" or "+18/+22°" for forecast */
    char humidity[16];  /*!< e.g. "56%" */
    char updated[8];    /*!< last update "HH:MM" */
} weather_info_t;

typedef struct {
    bool valid;
    char date[12];        /*!< "07-15" / "07-16" */
    char cond[48];
    char temp[24];        /*!< "16-22°" range */
} weather_forecast_t;

/** Configure timezone (CST-8) and start SNTP. */
void net_time_start(void);

/** True once the system clock has been set by SNTP. */
bool net_time_is_synced(void);

/** Start the periodic weather fetch task (call after the network is up). */
void net_weather_start(void);

/** Copy the latest weather snapshot; returns weather_info_t.valid. */
bool net_weather_get(weather_info_t *out);

/** Copy the 3-day forecast; returns weather_forecast_t.valid for [0]. */
bool net_weather_get_forecast(weather_forecast_t out[3]);

/** Set the query city (ASCII, e.g. "Changsha"/"Beijing"); persists to NVS and refetches. */
void net_weather_set_city(const char *city);

/** Get the current query city. */
const char *net_weather_get_city(void);

/** Select temperature unit: true = Fahrenheit, false = Celsius. Persists and refetches. */
void net_weather_set_fahrenheit(bool fahrenheit);

/** Current temperature unit; true = Fahrenheit. */
bool net_weather_get_fahrenheit(void);

#ifdef __cplusplus
}
#endif

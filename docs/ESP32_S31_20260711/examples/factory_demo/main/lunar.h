/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int year;
    int month;
    int day;
    int is_leap;
} lunar_date_t;

/** Convert Gregorian (year/month/day) to Chinese lunar date. */
lunar_date_t lunar_from_gregorian(int year, int month, int day);

/** Format lunar date into a human-readable Chinese string (e.g. "六月廿一").
 *  out must hold at least 32 bytes. */
void lunar_format(char *out, size_t out_len, lunar_date_t ld);

#ifdef __cplusplus
}
#endif

/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "lunar.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

/* Forward declarations (mutually recursive) */
static int lunar_leap_days(int y);
static int lunar_year_days(int y);
static int lunar_leap_month(int y);
static int lunar_month_days(int y, int m);

/* Lunar year data 1900–2100: each uint32_t packs month-length bits + leap month (low 4 bits). */
static const unsigned int LUNAR_INFO[] = {
    0x04bd8, 0x04ae0, 0x0a570, 0x054d5, 0x0d260, 0x0d950, 0x16554, 0x056a0,
    0x09ad0, 0x055d2, 0x04ae0, 0x0a5b6, 0x0a4d0, 0x0d250, 0x1d255, 0x0b540,
    0x0d6a0, 0x0ada2, 0x095b0, 0x14977, 0x04970, 0x0a4b0, 0x0b4b5, 0x06a50,
    0x06d40, 0x1ab54, 0x02b60, 0x09570, 0x052f2, 0x04970, 0x06566, 0x0d4a0,
    0x0ea50, 0x06e95, 0x05ad0, 0x02b60, 0x186e3, 0x092e0, 0x1c8d7, 0x0c950,
    0x0d4a0, 0x1d8a6, 0x0b550, 0x056a0, 0x1a5b4, 0x025d0, 0x092d0, 0x0d2b2,
    0x0a950, 0x0b557, 0x06ca0, 0x0b550, 0x15355, 0x04da0, 0x0a5b0, 0x14573,
    0x052b0, 0x0a9a8, 0x0e950, 0x06aa0, 0x0aea6, 0x0ab50, 0x04b60, 0x0aae4,
    0x0a570, 0x05260, 0x0f263, 0x0d950, 0x05b57, 0x056a0, 0x096d0, 0x04dd5,
    0x04ad0, 0x0a4d0, 0x0d4d4, 0x0d250, 0x0d558, 0x0b540, 0x0b6a0, 0x195a6,
    0x095b0, 0x049b0, 0x0a974, 0x0a4b0, 0x0b27a, 0x06a50, 0x06d40, 0x0af46,
    0x0ab60, 0x09570, 0x04af5, 0x04970, 0x064b0, 0x074a3, 0x0ea50, 0x06b58,
    0x055c0, 0x0ab60, 0x096d5, 0x092e0, 0x0c960, 0x0d954, 0x0d4a0, 0x0da50,
    0x07552, 0x056a0, 0x0abb7, 0x025d0, 0x092d0, 0x0cab5, 0x0a950, 0x0b4a0,
    0x0baa4, 0x0ad50, 0x055d9, 0x04ba0, 0x0a5b0, 0x15176, 0x052b0, 0x0a930,
    0x07954, 0x06aa0, 0x0ad50, 0x05b52, 0x04b60, 0x0a6e6, 0x0a4e0, 0x0d260,
    0x0ea65, 0x0d530, 0x05aa0, 0x076a3, 0x096d0, 0x04afb, 0x04ad0, 0x0a4d0,
    0x1d0b6, 0x0d250, 0x0d520, 0x0dd45, 0x0b5a0, 0x056d0, 0x055b2, 0x049b0,
    0x0a577, 0x0a4b0, 0x0aa50, 0x1b255, 0x06d20, 0x0ada0, 0x14b63,
};

static int lunar_year_days(int y)
{
    int i, sum = 348;
    for (i = 0x8000; i > 0x8; i >>= 1) {
        sum += (LUNAR_INFO[y - 1900] & i) ? 1 : 0;
    }
    return sum + lunar_leap_days(y);
}

static int lunar_leap_month(int y)
{
    return LUNAR_INFO[y - 1900] & 0xf;
}

static int lunar_leap_days(int y)
{
    if (lunar_leap_month(y)) {
        return (LUNAR_INFO[y - 1900] & 0x10000) ? 30 : 29;
    }
    return 0;
}

static int lunar_month_days(int y, int m)
{
    return (LUNAR_INFO[y - 1900] & (0x10000 >> m)) ? 30 : 29;
}

static long gregorian_to_jd(int year, int month, int day)
{
    struct tm t = {0};
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = day;
    t.tm_hour = 12;
    time_t ts = mktime(&t);
    return (long)(ts / 86400) + 2440588L; /* days since 1970 + JD offset */
}

lunar_date_t lunar_from_gregorian(int year, int month, int day)
{
    lunar_date_t r = {0};
    long offset = gregorian_to_jd(year, month, day) -
                  gregorian_to_jd(1900, 1, 31);

    int y = 1900;
    while (y < 2101 && offset > 0) {
        offset -= lunar_year_days(y);
        y++;
    }
    if (offset < 0) {
        offset += lunar_year_days(--y);
    }

    r.year = y;
    int leap = lunar_leap_month(y);
    int is_leap = 0;
    int m;
    for (m = 1; m < 13 && offset > 0; m++) {
        int days;
        if (leap > 0 && m == (leap + 1) && !is_leap) {
            --m;
            is_leap = 1;
            days = lunar_leap_days(r.year);
        } else {
            days = lunar_month_days(r.year, m);
        }
        if (is_leap && m == (leap + 1)) {
            is_leap = 0;
        }
        offset -= days;
    }
    if (offset == 0 && leap == m && is_leap) {
        if (is_leap) {
            is_leap = 0;
        } else {
            is_leap = 1;
            --m;
        }
    }
    if (offset < 0) {
        offset += lunar_month_days(r.year, --m);
    }
    r.month = m;
    r.day = (int)(offset + 1);
    r.is_leap = is_leap && (leap == m);
    return r;
}

void lunar_format(char *out, size_t out_len, lunar_date_t ld)
{
    static const char *m_names[] = {
        "", "\u6b63", "\u4e8c", "\u4e09", "\u56db", "\u4e94",
        "\u516d", "\u4e03", "\u516b", "\u4e5d", "\u5341",
        "\u5341\u4e00", "\u5341\u4e8c"}; /* 正～十二 */

    static const char *d_names[] = {
        "\u521d\u4e00", "\u521d\u4e8c", "\u521d\u4e09", "\u521d\u56db",
        "\u521d\u4e94", "\u521d\u516d", "\u521d\u4e03", "\u521d\u516b",
        "\u521d\u4e5d", "\u521d\u5341", "\u5341\u4e00", "\u5341\u4e8c",
        "\u5341\u4e09", "\u5341\u56db", "\u5341\u4e94", "\u5341\u516d",
        "\u5341\u4e03", "\u5341\u516b", "\u5341\u4e5d", "\u4e8c\u5341",
        "\u5eff\u4e00", "\u5eff\u4e8c", "\u5eff\u4e09", "\u5eff\u56db",
        "\u5eff\u4e94", "\u5eff\u516d", "\u5eff\u4e03", "\u5eff\u516b",
        "\u5eff\u4e5d", "\u4e09\u5341"
    }; /* 初一～三十 */

    int day = ld.day;
    if (day < 1) { day = 1; }
    if (day > 30) { day = 30; }

    snprintf(out, out_len, "%s%s月%s",
             ld.is_leap ? "\u95f0" : "",
             m_names[ld.month <= 12 ? ld.month : 1],
             d_names[day - 1]);
}

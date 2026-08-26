/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include "bsp/esp32_s31_korvo.h"
#include "esp_log.h"
#include "lvgl.h"
#include "wx_icons.h"
#include "net_wifi.h"
#include "net_weather.h"

LV_FONT_DECLARE(font_wc_cn_18);
LV_FONT_DECLARE(font_wc_cn_24);
LV_FONT_DECLARE(font_wc_emoji_46);
LV_FONT_DECLARE(font_wc_emoji_24);

static const char *TAG = "weather_clock";

/* ---- Layout constants ---- */
#define TOP_BAR_H         50
#define BOTTOM_BAR_H      100
#define PANEL_GAP          8
#define LEFT_PANEL_W      392
#define RIGHT_PANEL_W     392
#define MID_PANEL_H       328
#define 8        54
#define BOTTOM_BAR_Y      386

/* ---- Theme colors ---- */
static const lv_color_t COLOR_BG      = LV_COLOR_MAKE(0x0D, 0x1B, 0x2A);
static const lv_color_t COLOR_PANEL   = LV_COLOR_MAKE(0x10, 0x1F, 0x32);
static const lv_color_t COLOR_ACCENT  = LV_COLOR_MAKE(0xFF, 0x8A, 0x5B);
static const lv_color_t COLOR_TEXT    = LV_COLOR_MAKE(0xF4, 0xF5, 0xF7);
static const lv_color_t COLOR_MUTED   = LV_COLOR_MAKE(0x9E, 0xAE, 0xBF);
static const lv_color_t COLOR_SKY     = LV_COLOR_MAKE(0x61, 0xA8, 0xFF);

/* ---- Weather types ---- */
typedef enum {
    WEATHER_SUNNY,
    WEATHER_CLOUDY,
    WEATHER_PARTLY_CLOUDY,
    WEATHER_RAINY,
    WEATHER_STORMY,
    WEATHER_SNOWY,
    WEATHER_WINDY,
    WEATHER_COUNT
} weather_t;

static const char *weather_names[] = {
    "鏅?, "澶氫簯", "闃?, "灏忛洦", "闆烽洦", "闆?, "澶ч"
};

static const char *weather_icons[] = {
    "鈽€", "鈽?, "鉀?, "馃導", "鉀?, "鉂?, "馃挩"
};

/* ---- Lunar calendar data table (1900-2100) ---- */
/* Encoding: bits 0-3 = leap month (0 = none), bit 4 = leap days (0=29, 1=30),
   bits 5-16 = month 1-12 days starting from LSB (0=29, 1=30) */
static const uint32_t lunar_table[] = {
    0x04bd8, 0x04ae0, 0x0a570, 0x054d5, 0x0d260, 0x0d950, 0x16554, 0x056a0, 0x09ad0, 0x055d2,
    0x04ae0, 0x0a5b6, 0x0a4d0, 0x0d250, 0x1d255, 0x0b540, 0x0d6a0, 0x0ada2, 0x095b0, 0x14977,
    0x04970, 0x0a4b0, 0x0b4b5, 0x06a50, 0x06d40, 0x1ab54, 0x02b60, 0x09570, 0x052f2, 0x04970,
    0x06566, 0x0d4a0, 0x0ea50, 0x06e95, 0x05ad0, 0x02b60, 0x186e3, 0x092e0, 0x1c8d7, 0x0c950,
    0x0d4a0, 0x1d8a6, 0x0b550, 0x056a0, 0x1a5b4, 0x025d0, 0x092d0, 0x0d2b2, 0x0a950, 0x0b557,
    0x06ca0, 0x0b550, 0x15355, 0x04da0, 0x0a5b0, 0x14573, 0x052b0, 0x0a9a8, 0x0e950, 0x06aa0,
    0x0aea6, 0x0ab50, 0x04b60, 0x0aae4, 0x0a570, 0x05260, 0x0f263, 0x0d950, 0x05b57, 0x056a0,
    0x096d0, 0x04dd5, 0x04ad0, 0x0a4d0, 0x0d4d4, 0x0d250, 0x0d558, 0x0b540, 0x0b6a0, 0x195a6,
    0x095b0, 0x049b0, 0x0a974, 0x0a4b0, 0x0b27a, 0x06a50, 0x06d40, 0x0af46, 0x0ab60, 0x09570,
    0x04af5, 0x04970, 0x064b0, 0x074a3, 0x0ea50, 0x06b58, 0x05ac0, 0x0ab60, 0x096d5, 0x092e0,
    0x0c960, 0x0d954, 0x0d4a0, 0x0da50, 0x07552, 0x056a0, 0x0abb7, 0x025d0, 0x092d0, 0x0cab5,
    0x0a950, 0x0b4a0, 0x0baa4, 0x0ad50, 0x055d9, 0x04ba0, 0x0a5b0, 0x15176, 0x052b0, 0x0a930,
    0x07954, 0x06aa0, 0x0ad50, 0x05b52, 0x04b60, 0x0a6e6, 0x0a4e0, 0x0d260, 0x0ea65, 0x0d530,
    0x05aa0, 0x076a3, 0x096d0, 0x04afb, 0x04ad0, 0x0a4d0, 0x1d0b6, 0x0d250, 0x0d520, 0x0dd45,
    0x0b5a0, 0x056d0, 0x055b2, 0x049b0, 0x0a577, 0x0a4b0, 0x0aa50, 0x1b255, 0x06d20, 0x0ada0,
    0x14b63, 0x09370, 0x049f8, 0x04970, 0x064b0, 0x168a6, 0x0ea50, 0x06b20, 0x1a6c4, 0x0aae0,
    0x0a2e0, 0x0d2e3, 0x0c960, 0x0d557, 0x0d4a0, 0x0da50, 0x05d55, 0x056a0, 0x0a6d0, 0x055d4,
    0x052d0, 0x0a9b8, 0x0a950, 0x0b4a0, 0x0b6a6, 0x0ad50, 0x055a0, 0x0aba4, 0x0a5b0, 0x052b0,
    0x0b273, 0x06930, 0x07337, 0x06aa0, 0x0ad50, 0x14b55, 0x04b60, 0x0a570, 0x054e4, 0x0d160,
    0x0e968, 0x0d520, 0x0daa0, 0x16aa6, 0x056d0, 0x04ae0, 0x0a9d4, 0x0a4d0, 0x0d150, 0x0f252,
    0x0d520
};

static const char *lunar_month_names[] = {
    "姝?, "浜?, "涓?, "鍥?, "浜?, "鍏?, "涓?, "鍏?, "涔?, "鍗?, "鍗佷竴", "鍗佷簩"
};

static const char *lunar_day_short[] = {
    "鍒濅竴", "鍒濅簩", "鍒濅笁", "鍒濆洓", "鍒濅簲", "鍒濆叚", "鍒濅竷", "鍒濆叓", "鍒濅節", "鍒濆崄",
    "鍗佷竴", "鍗佷簩", "鍗佷笁", "鍗佸洓", "鍗佷簲", "鍗佸叚", "鍗佷竷", "鍗佸叓", "鍗佷節", "浜屽崄",
    "寤夸竴", "寤夸簩", "寤夸笁", "寤垮洓", "寤夸簲", "寤垮叚", "寤夸竷", "寤垮叓", "寤夸節", "涓夊崄"
};

static const char *weekday_names[] = { "鏃?, "涓€", "浜?, "涓?, "鍥?, "浜?, "鍏? };

/* ---- Lunar calendar utilities ---- */
static int lunar_leap_month(int year)
{
    return lunar_table[year - 1900] & 0xf;
}

static int lunar_leap_days(int year)
{
    if (lunar_leap_month(year)) {
        return (lunar_table[year - 1900] & 0x10) ? 30 : 29;
    }
    return 0;
}

static int lunar_month_days(int year, int month)
{
    return (lunar_table[year - 1900] & (0x10000 >> (month - 1))) ? 30 : 29;
}

static int lunar_year_days(int year)
{
    int sum = 348;
    for (int i = 0x8000; i > 0x8; i >>= 1) {
        sum += (lunar_table[year - 1900] & i) ? 1 : 0;
    }
    return sum + lunar_leap_days(year);
}

static int solar_days_in_year(int year)
{
    return ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) ? 366 : 365;
}

static int solar_days_in_month(int year, int month)
{
    static const int mdays[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    int d = mdays[month - 1];
    if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0))) {
        d = 29;
    }
    return d;
}

static void solar_to_lunar(int sy, int sm, int sd,
                           int *ly, int *lm, int *ld, int *is_leap)
{
    int total = 0;
    int y, m;

    for (y = 1900; y < sy; y++) {
        total += solar_days_in_year(y);
    }
    for (m = 1; m < sm; m++) {
        total += solar_days_in_month(sy, m);
    }
    total += sd - 1;

    if (total < 30) {
        *ly = 1899;
        *lm = 12;
        *ld = total + 1;
        *is_leap = 0;
        return;
    }
    total -= 30;

    y = 1900;
    while (y <= 2100) {
        int yd = lunar_year_days(y);
        if (total < yd) {
            break;
        }
        total -= yd;
        y++;
    }
    *ly = y;

    int leap_m = lunar_leap_month(y);
    *is_leap = 0;
    *lm = 1;
    *ld = 1;

    for (m = 1; m <= 12; m++) {
        int md = lunar_month_days(y, m);
        if (total < md) {
            *lm = m;
            *ld = total + 1;
            return;
        }
        total -= md;

        if (m == leap_m) {
            int lmd = lunar_leap_days(y);
            if (total < lmd) {
                *lm = m;
                *ld = total + 1;
                *is_leap = 1;
                return;
            }
            total -= lmd;
        }
    }
}

/* ---- UI object references ---- */
typedef struct {
    lv_obj_t *screen;
    lv_obj_t *parent_screen;
    lv_obj_t *city_label;
    lv_obj_t *current_weather_icon;
    lv_obj_t *current_weather_text;
    lv_obj_t *current_temp;
    lv_obj_t *current_humidity;
    lv_obj_t *forecast_day_labels[3];
    lv_obj_t *forecast_icons[3];
    lv_obj_t *forecast_texts[3];
    lv_obj_t *forecast_temps[3];
    lv_obj_t *time_label;
    lv_obj_t *date_label;
    lv_timer_t *timer;
    int weather_index;
    int tick_count;
} weather_clock_t;

static weather_clock_t *s_wc;

/* ---- Helper: style a rounded panel ---- */
static void weather_style_panel(lv_obj_t *obj, lv_color_t color)
{
    lv_obj_set_style_radius(obj, 22, 0);
    lv_obj_set_style_bg_color(obj, color, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(0x203348), 0);
    lv_obj_set_style_shadow_width(obj, 16, 0);
    lv_obj_set_style_shadow_opa(obj, LV_OPA_20, 0);
    lv_obj_set_style_shadow_color(obj, lv_color_black(), 0);
    lv_obj_set_style_pad_all(obj, 16, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

/* ---- Helper: create centered label with font ---- */
static lv_obj_t *weather_make_label(lv_obj_t *parent, const char *text,
                                     const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    if (text) {
        lv_label_set_text(label, text);
    }
    return label;
}

/* ---- Weather simulation ---- */
static const char *weather_cond_icon(const char *cond)
{
    if (strstr(cond, "\u96F7")) return "\u26C8";
    if (strstr(cond, "\u96EA")) return "\u2744";
    if (strstr(cond, "\u96E8")) return "\xF0\x9F\x8C\xA7";
    if (strstr(cond, "\u96FE")||strstr(cond, "\u9708")) return "\xF0\x9F\x92\xA8";
    if (strstr(cond, "\u591A\u4E91")) return "\u26C5";
    if (strstr(cond, "\u9634")) return "\u2601";
    if (strstr(cond, "\u6674")) return "\u2600";
    if (strstr(cond, "\u98CE")) return "\xF0\x9F\x92\xA8";
    return "\u2600";
}

static void weather_timer_cb(lv_timer_t *timer)
{
    weather_clock_t *wc = (weather_clock_t *)lv_timer_get_user_data(timer);
    if (!wc || !wc->screen) return;
    if (lv_disp_get_scr_act(NULL) != wc->screen) return;

    static bool svc;
    if (net_wifi_get_state() == NET_WIFI_CONNECTED && !svc) {
        svc = true; net_time_start(); net_weather_start();
    }

    time_t now; struct tm ti;
    time(&now); localtime_r(&now, &ti);
    lv_label_set_text_fmt(wc->time_label, "%02d:%02d:%02d", ti.tm_hour, ti.tm_min, ti.tm_sec);

    int ly, lm, ld, is_leap;
    solar_to_lunar(ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday, &ly, &lm, &ld, &is_leap);
    char ls[32];
    if (ld >= 1 && ld <= 30 && lm >= 1 && lm <= 12) {
        snprintf(ls, sizeof(ls), "%s%s%s", is_leap ? "\u95F0" : "", lunar_month_names[lm-1], "\u6708");
        strlcat(ls, lunar_day_short[ld-1], sizeof(ls));
    } else snprintf(ls, sizeof(ls), "--");
    lv_label_set_text_fmt(wc->date_label, "%d\u5E74%02d\u6708%02d\u65E5  %s  \u661F\u671F%s",
        ti.tm_year+1900, ti.tm_mon+1, ti.tm_mday, ls, weekday_names[ti.tm_wday]);

    weather_info_t w;
    if (net_weather_get(&w) && w.valid) {
        lv_label_set_text(wc->current_weather_icon, weather_cond_icon(w.cond));
        lv_label_set_text(wc->current_weather_text, w.cond);
        lv_label_set_text(wc->current_temp, w.temp);
        char hb[32]; snprintf(hb, sizeof(hb), "\u6E7F\u5EA6: %s", w.humidity);
        lv_label_set_text(wc->current_humidity, hb);
    }
    weather_forecast_t fc[3];
    if (net_weather_get_forecast(fc))
        for (int i = 0; i < 3; i++)
            if (fc[i].valid) {
                lv_label_set_text(wc->forecast_texts[i], fc[i].cond);
                lv_label_set_text(wc->forecast_temps[i], fc[i].temp);
            }
}

/* ---- Back button callback ---- */
static void on_back_btn(lv_event_t *e)
{
    weather_clock_t *wc = (weather_clock_t *)lv_event_get_user_data(e);
    if (!wc || !wc->parent_screen) {
        return;
    }
    lv_screen_load_anim(wc->parent_screen, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
}

/* ---- Public API ---- */
void weather_clock_create(lv_obj_t *parent)
{
    if (s_wc) {
        if (bsp_display_lock(0)) {
            lv_screen_load_anim(s_wc->screen, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
            bsp_display_unlock();
        }
        return;
    }

    weather_clock_t *wc = calloc(1, sizeof(weather_clock_t));
    if (!wc) {
        ESP_LOGE(TAG, "Failed to allocate weather clock");
        return;
    }
    s_wc = wc;
    wc->parent_screen = parent;
    wc->weather_index = WEATHER_SUNNY;
    wc->tick_count = 0;

    /* ---- Create screen ---- */
    lv_obj_t *scr = lv_obj_create(NULL);
    wc->screen = scr;
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* ---- Decorative orbs ---- */
    lv_obj_t *orb = lv_obj_create(scr);
    lv_obj_remove_style_all(orb);
    lv_obj_set_size(orb, 200, 200);
    lv_obj_set_pos(orb, -50, -30);
    lv_obj_set_style_radius(orb, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(orb, COLOR_ACCENT, 0);
    lv_obj_set_style_bg_opa(orb, 38, 0);

    orb = lv_obj_create(scr);
    lv_obj_remove_style_all(orb);
    lv_obj_set_size(orb, 160, 160);
    lv_obj_set_pos(orb, 680, 320);
    lv_obj_set_style_radius(orb, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(orb, COLOR_SKY, 0);
    lv_obj_set_style_bg_opa(orb, 31, 0);

    /* ===== LEFT PANEL: Current Weather ===== */
    lv_obj_t *left_panel = lv_obj_create(scr);
    lv_obj_t *wifi_icon = lv_label_create(left_panel);
    lv_label_set_text(wifi_icon, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_font(wifi_icon, &lv_font_montserrat_24, 0);
    lv_obj_align(wifi_icon, LV_ALIGN_TOP_RIGHT, -8, 8);

    lv_obj_set_pos(left_panel, 4, 8);
    lv_obj_set_size(left_panel, LEFT_PANEL_W, MID_PANEL_H);
    weather_style_panel(left_panel, COLOR_PANEL);

    /* Panel title */
    lv_obj_t *left_title = weather_make_label(left_panel, "褰撳墠澶╂皵", &font_wc_cn_18, COLOR_TEXT);
    lv_obj_align(left_title, LV_ALIGN_TOP_LEFT, 0, 0);

    /* Weather icon - large */
    wc->current_weather_icon = weather_make_label(left_panel, "鈽€", &font_wc_emoji_46, COLOR_ACCENT);
    lv_obj_set_style_text_align(wc->current_weather_icon, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(wc->current_weather_icon, LV_ALIGN_CENTER, 0, -40);

    /* Weather name */
    wc->current_weather_text = weather_make_label(left_panel, "鏅?, &font_wc_cn_24, COLOR_TEXT);
    lv_obj_set_style_text_align(wc->current_weather_text, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align_to(wc->current_weather_text, wc->current_weather_icon, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);

    /* Temperature */
    wc->current_temp = weather_make_label(left_panel, "17掳C", &font_wc_cn_24, COLOR_ACCENT);
    lv_obj_set_style_text_align(wc->current_temp, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align_to(wc->current_temp, wc->current_weather_text, LV_ALIGN_OUT_BOTTOM_MID, 0, 12);

    /* Humidity detail */
    wc->current_humidity = weather_make_label(left_panel, "婀垮害: 65%", &font_wc_cn_24, COLOR_MUTED);
    lv_obj_set_style_text_align(wc->current_humidity, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align_to(wc->current_humidity, wc->current_temp, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

    /* ===== RIGHT PANEL: Forecast ===== */
    lv_obj_t *right_panel = lv_obj_create(scr);
    lv_obj_set_pos(right_panel, LEFT_PANEL_W + PANEL_GAP + 4, 8);
    lv_obj_set_size(right_panel, RIGHT_PANEL_W, MID_PANEL_H);
    weather_style_panel(right_panel, COLOR_PANEL);

    /* Panel title */
    lv_obj_t *right_title = weather_make_label(right_panel, "鏈潵棰勬姤", &font_wc_cn_18, COLOR_TEXT);
    lv_obj_align(right_title, LV_ALIGN_TOP_LEFT, 0, 0);

    /* Day labels across top */
    static const char *day_labels[] = { "浠婂ぉ", "鏄庡ぉ", "鍚庡ぉ" };
    int col_w = RIGHT_PANEL_W / 3 - 16;
    int col_x_base = 8;

    for (int i = 0; i < 3; i++) {
        int cx = col_x_base + i * (col_w + 12);

        /* Container for each forecast column */
        lv_obj_t *col = lv_obj_create(right_panel);
        lv_obj_set_pos(col, cx, 30);
        lv_obj_set_size(col, col_w, 270);
        lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(col, LV_OPA_0, 0);
        lv_obj_set_style_border_width(col, 0, 0);
        lv_obj_set_style_pad_all(col, 0, 0);

        /* Day name */
        wc->forecast_day_labels[i] = weather_make_label(col, day_labels[i],
            &font_wc_cn_18, COLOR_MUTED);
        lv_obj_set_style_text_align(wc->forecast_day_labels[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(wc->forecast_day_labels[i], LV_ALIGN_TOP_MID, 0, 4);

        /* Icon */
        wc->forecast_icons[i] = weather_make_label(col, weather_icons[i],
            &font_wc_emoji_46, COLOR_ACCENT);
        lv_obj_set_style_text_align(wc->forecast_icons[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align_to(wc->forecast_icons[i], wc->forecast_day_labels[i],
            LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

        /* Weather text */
        wc->forecast_texts[i] = weather_make_label(col, weather_names[i],
            &font_wc_cn_18, COLOR_TEXT);
        lv_obj_set_style_text_align(wc->forecast_texts[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align_to(wc->forecast_texts[i], wc->forecast_icons[i],
            LV_ALIGN_OUT_BOTTOM_MID, 0, 8);

        /* Temperature */
        wc->forecast_temps[i] = weather_make_label(col, "22掳",
            &font_wc_cn_24, COLOR_ACCENT);
        lv_obj_set_style_text_align(wc->forecast_temps[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align_to(wc->forecast_temps[i], wc->forecast_texts[i],
            LV_ALIGN_OUT_BOTTOM_MID, 0, 8);
    }

    /* ===== BOTTOM BAR ===== */
    lv_obj_t *bottom_bar = lv_obj_create(scr);
    lv_obj_set_pos(bottom_bar, 4, BOTTOM_BAR_Y);
    lv_obj_set_size(bottom_bar, 792, BOTTOM_BAR_H);
    lv_obj_clear_flag(bottom_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(bottom_bar, COLOR_PANEL, 0);
    lv_obj_set_style_bg_opa(bottom_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bottom_bar, 18, 0);
    lv_obj_set_style_border_width(bottom_bar, 1, 0);
    lv_obj_set_style_border_color(bottom_bar, lv_color_hex(0x203348), 0);
    lv_obj_set_style_pad_all(bottom_bar, 0, 0);

    /* Time display - large */
    wc->time_label = weather_make_label(bottom_bar, "00:00:00", &font_wc_emoji_46, COLOR_TEXT);
    lv_obj_set_style_text_align(wc->time_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(wc->time_label, LV_ALIGN_CENTER, 0, -16);

    /* Date + lunar + weekday */
    wc->date_label = weather_make_label(bottom_bar, "0000骞?0鏈?0鏃? --  鏄熸湡-", &font_wc_cn_18, COLOR_MUTED);
    lv_obj_set_style_text_align(wc->date_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(wc->date_label, LV_ALIGN_CENTER, 0, 28);

    /* ===== BACK BUTTON ===== */
    lv_obj_t *back_btn = lv_btn_create(scr);
    lv_obj_add_flag(back_btn, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(back_btn, 100, 36);
    lv_obj_align(back_btn, LV_ALIGN_BOTTOM_RIGHT, -10, -8);
    lv_obj_set_style_radius(back_btn, 18, 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x1A2D43), 0);
    lv_obj_set_style_bg_opa(back_btn, LV_OPA_80, 0);
    lv_obj_set_style_shadow_width(back_btn, 0, 0);
    lv_obj_set_style_border_width(back_btn, 1, 0);
    lv_obj_set_style_border_color(back_btn, COLOR_ACCENT, 0);
    lv_obj_add_event_cb(back_btn, on_back_btn, LV_EVENT_CLICKED, wc);

    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_obj_set_style_text_font(back_label, &font_wc_cn_18, 0);
    lv_obj_set_style_text_color(back_label, COLOR_TEXT, 0);
    lv_label_set_text(back_label, "杩斿洖");
    lv_obj_center(back_label);
    lv_obj_move_foreground(back_btn);

    /* ===== START TIMER ===== */
    wc->timer = lv_timer_create(weather_timer_cb, 500, wc);

    /* Load the screen */
    if (bsp_display_lock(0)) {
        lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
        bsp_display_unlock();
    }
}

/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "app_ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "esp_log.h"
#include "lunar.h"
#include "lvgl.h"
#include "net_wifi.h"
#include "net_weather.h"

LV_FONT_DECLARE(font_wc_cn_16);
LV_FONT_DECLARE(font_wc_cn_18);
LV_FONT_DECLARE(font_wc_cn_20);
LV_FONT_DECLARE(font_wc_cn_22);
LV_FONT_DECLARE(font_wc_cn_24);
LV_FONT_DECLARE(font_wc_cn_32);
LV_FONT_DECLARE(font_wc_emoji_24);
LV_FONT_DECLARE(font_wc_emoji_46);
LV_FONT_DECLARE(font_wc_emoji_56);
LV_FONT_DECLARE(font_wc_emoji_100);
LV_FONT_DECLARE(font_clock_40);
LV_FONT_DECLARE(font_clock_60);
LV_FONT_DECLARE(lv_font_montserrat_24);

static const char *TAG = "app_ui";

static const char *wx_emoji(const char *cond);
static void on_wifi_open(lv_event_t *e);
static void update_wifi_dd(void);

static const lv_color_t C_TEXT   = LV_COLOR_MAKE(0xF2,0xF5,0xF8);
static const lv_color_t C_MUTED  = LV_COLOR_MAKE(0x9A,0xAC,0xC0);
static const lv_color_t C_ACCENT = LV_COLOR_MAKE(0x4F,0xA8,0xFF);

static lv_obj_t *s_info_scr;
static lv_obj_t *s_tab_now, *s_tab_fc;
static lv_obj_t *s_fc[3];
static lv_obj_t *s_wicon, *s_wname, *s_loc, *s_temp, *s_humi;
static lv_obj_t *s_time, *s_date, *s_nongli, *s_week;
static lv_obj_t *s_orb1, *s_orb2;
static lv_obj_t *s_drops[35];
static lv_obj_t *s_flakes[30];
static lv_obj_t *s_clouds[5][4];
static lv_obj_t *s_fogbands[6];
static lv_obj_t *s_flash;
static lv_obj_t *s_wifi_icon;
static float s_drop_spd[35], s_flake_spd[30], s_flake_ph[30], s_cloud_spd[5];
static int s_cloud_y[5];
static bool s_svc;
static int s_bg_tick;
static lv_obj_t *s_wifi_scr;
static lv_obj_t *s_ssid_dd, *s_pass_ta, *s_wifi_status;
static bool s_scan_pending;

static const char *WK[7] = {"日","一","二","三","四","五","六"};

static lv_obj_t *mk_lbl(lv_obj_t *p, const char *t, lv_color_t c, const lv_font_t *f)
{
    lv_obj_t *l = lv_label_create(p);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    if (t) lv_label_set_text(l, t);
    return l;
}

static const char *wx_emoji(const char *cond)
{
    if (strstr(cond, "\u96F7")) return "\u26C8";
    if (strstr(cond, "\u51B0\u96F9")) return "\u2744";
    if (strstr(cond, "\u96EA")) return "\xF0\x9F\x8C\xA8";
    if (strstr(cond, "\u5C0F\u96E8")) return "\xF0\x9F\x8C\xA6";
    if (strstr(cond, "\u5927\u96E8") || strstr(cond, "\u9635\u96E8") || strstr(cond, "\u96E8"))
        return "\xF0\x9F\x8C\xA7";
    if (strstr(cond, "\u96FE") || strstr(cond, "\u9708")) return "\xF0\x9F\x8C\xAB";
    if (strstr(cond, "\u591A\u4E91")) return "\u26C5";
    if (strstr(cond, "\u9634")) return "\u2601";
    if (strstr(cond, "\u6674")) return "\u2600";
    if (strstr(cond, "\u98CE")) return "\xF0\x9F\x92\xA8";
    return "\u2600";
}

static void build_info(void)
{
    s_info_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_info_scr, lv_color_hex(0x060D18), 0);
    lv_obj_set_style_bg_opa(s_info_scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_info_scr, LV_OBJ_FLAG_SCROLLABLE);

    s_orb1 = lv_obj_create(s_info_scr);
    lv_obj_remove_style_all(s_orb1);
    lv_obj_set_size(s_orb1, 160, 160);
    lv_obj_set_pos(s_orb1, -40, 60);
    lv_obj_set_style_radius(s_orb1, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_orb1, 0, 0);
    lv_obj_clear_flag(s_orb1, LV_OBJ_FLAG_SCROLLABLE);

    s_orb2 = lv_obj_create(s_info_scr);
    lv_obj_remove_style_all(s_orb2);
    lv_obj_set_size(s_orb2, 120, 120);
    lv_obj_set_pos(s_orb2, 680, 340);
    lv_obj_set_style_radius(s_orb2, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_orb2, 0, 0);
    lv_obj_clear_flag(s_orb2, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < 35; i++) {
        s_drops[i] = lv_obj_create(s_info_scr);
        lv_obj_remove_style_all(s_drops[i]);
        lv_obj_set_size(s_drops[i], 2, 8 + (i % 7));
        lv_obj_set_style_bg_color(s_drops[i], lv_color_hex(0xB4D2FF), 0);
        lv_obj_set_style_bg_opa(s_drops[i], 0, 0);
        lv_obj_set_style_radius(s_drops[i], 1, 0);
        s_drop_spd[i] = 4.0f + (float)(i % 7) * 1.5f;
    }
    for (int i = 0; i < 30; i++) {
        s_flakes[i] = lv_obj_create(s_info_scr);
        lv_obj_remove_style_all(s_flakes[i]);
        int r = 2 + (i % 3);
        lv_obj_set_size(s_flakes[i], r * 2, r * 2);
        lv_obj_set_style_radius(s_flakes[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(s_flakes[i], lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_bg_opa(s_flakes[i], 0, 0);
        s_flake_spd[i] = 0.5f + (float)(i % 5) * 0.5f;
        s_flake_ph[i] = (float)(i * 13) * 0.1f;
    }
    for (int i = 0; i < 5; i++) {
        s_cloud_y[i] = 40 + i * 55;
        s_cloud_spd[i] = 0.3f + (float)i * 0.15f;
        int cx = 100 + i * 140;
        /* 4 overlapping circles form a realistic cloud puff */
        int radii[4] = {28, 20, 22, 18};
        int dx[4] = {0, 22, -18, 10};
        int dy[4] = {0, -8, 2, -12};
        for (int j = 0; j < 4; j++) {
            s_clouds[i][j] = lv_obj_create(s_info_scr);
            lv_obj_remove_style_all(s_clouds[i][j]);
            lv_obj_set_size(s_clouds[i][j], radii[j]*2, radii[j]*2);
            lv_obj_set_pos(s_clouds[i][j], cx + dx[j], s_cloud_y[i] + dy[j]);
            lv_obj_set_style_radius(s_clouds[i][j], LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(s_clouds[i][j], lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_bg_opa(s_clouds[i][j], 0, 0);
        }
    }
    for (int i = 0; i < 6; i++) {
        s_fogbands[i] = lv_obj_create(s_info_scr);
        lv_obj_remove_style_all(s_fogbands[i]);
        lv_obj_set_size(s_fogbands[i], 400 + i * 60, 16);
        lv_obj_set_style_radius(s_fogbands[i], 8, 0);
        lv_obj_set_style_bg_color(s_fogbands[i], lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_bg_opa(s_fogbands[i], 0, 0);
    }
    s_flash = lv_obj_create(s_info_scr);
    lv_obj_remove_style_all(s_flash);
    lv_obj_set_size(s_flash, 800, 480);
    lv_obj_set_style_bg_color(s_flash, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(s_flash, 0, 0);
    lv_obj_set_style_radius(s_flash, 0, 0);
    lv_obj_add_flag(s_flash, LV_OBJ_FLAG_HIDDEN);  /* block clicks only when visible */

    s_wifi_icon = lv_label_create(s_info_scr);
    lv_label_set_text(s_wifi_icon, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_font(s_wifi_icon, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_wifi_icon, lv_color_hex(0x8A97A5), 0);
    lv_obj_align(s_wifi_icon, LV_ALIGN_TOP_RIGHT, -16, 16);
    lv_obj_add_flag(s_wifi_icon, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(s_wifi_icon, 20);
    lv_obj_add_event_cb(s_wifi_icon, on_wifi_open, LV_EVENT_CLICKED, NULL);
    lv_obj_move_foreground(s_wifi_icon);

    /* NOW */
    s_tab_now = lv_obj_create(s_info_scr);
    lv_obj_set_size(s_tab_now, 750, 270);
    lv_obj_set_pos(s_tab_now, 20, 25);
    lv_obj_set_style_bg_opa(s_tab_now, 0, 0);
    lv_obj_set_style_border_width(s_tab_now, 0, 0);
    lv_obj_clear_flag(s_tab_now, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *wc = lv_obj_create(s_tab_now);
    lv_obj_set_size(wc, 700, 200);
    lv_obj_align(wc, LV_ALIGN_LEFT_MID, 25, 0);
    lv_obj_set_style_bg_opa(wc, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wc, 0, 0);
    lv_obj_set_flex_flow(wc, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(wc, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(wc, 10, 0);
    lv_obj_clear_flag(wc, LV_OBJ_FLAG_SCROLLABLE);

    s_wicon = mk_lbl(wc, "\u2600", lv_color_hex(0xFFB347), &font_wc_emoji_100);
    lv_label_set_text(s_wicon, "\u2600");
    s_wname = mk_lbl(wc, "\u5929\u6C14\u83B7\u53D6\u4E2D", C_TEXT, &font_wc_cn_22);
    s_loc = mk_lbl(wc, "\u957F\u6C99", C_MUTED, &font_wc_cn_22);

    lv_obj_t *c1 = lv_obj_create(wc);
    lv_obj_remove_style_all(c1);
    lv_obj_set_size(c1, 140, 140);
    lv_obj_set_flex_flow(c1, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(c1, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *tie = mk_lbl(c1, "\xF0\x9F\x8C\xA1", C_MUTED, &font_wc_emoji_24);
    lv_obj_set_style_text_font(tie, &font_wc_emoji_24, 0);
    mk_lbl(c1, "\u6E29\u5EA6", C_MUTED, &font_wc_cn_18);
    s_temp = mk_lbl(c1, "--\u2103", C_ACCENT, &font_wc_cn_32);

    lv_obj_t *c2 = lv_obj_create(wc);
    lv_obj_remove_style_all(c2);
    lv_obj_set_size(c2, 140, 140);
    lv_obj_set_flex_flow(c2, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(c2, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *hie = mk_lbl(c2, "\xF0\x9F\x92\xA7", C_MUTED, &font_wc_emoji_24);
    lv_obj_set_style_text_font(hie, &font_wc_emoji_24, 0);
    mk_lbl(c2, "\u6E7F\u5EA6", C_MUTED, &font_wc_cn_18);
    s_humi = mk_lbl(c2, "--%", C_ACCENT, &font_wc_cn_32);

    /* FORECAST */
    s_tab_fc = lv_obj_create(s_info_scr);
    lv_obj_set_size(s_tab_fc, 750, 270);
    lv_obj_set_pos(s_tab_fc, 20, 25);
    lv_obj_set_style_bg_opa(s_tab_fc, 0, 0);
    lv_obj_set_style_border_width(s_tab_fc, 0, 0);
    lv_obj_clear_flag(s_tab_fc, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_tab_fc, LV_OBJ_FLAG_HIDDEN);

    for (int i = 0; i < 3; i++) {
        s_fc[i] = lv_obj_create(s_tab_fc);
        lv_obj_set_size(s_fc[i], 220, 200);
        lv_obj_set_pos(s_fc[i], 25 + i * 240, 35);
        lv_obj_set_style_bg_opa(s_fc[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(s_fc[i], 0, 0);
        lv_obj_set_flex_flow(s_fc[i], LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(s_fc[i], LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(s_fc[i], LV_OBJ_FLAG_SCROLLABLE);

        mk_lbl(s_fc[i], "--", C_MUTED, &font_wc_cn_18);
        mk_lbl(s_fc[i], "\u2600", lv_color_hex(0xFFB347), &font_wc_emoji_46);
        mk_lbl(s_fc[i], "--", C_TEXT, &font_wc_cn_18);
        mk_lbl(s_fc[i], "--\u00B0", C_ACCENT, &font_wc_cn_20);
    }

    /* Divider */
    lv_obj_t *dv = lv_obj_create(s_info_scr);
    lv_obj_remove_style_all(dv);
    lv_obj_set_size(dv, 710, 1);
    lv_obj_set_pos(dv, 45, 295);
    lv_obj_set_style_bg_color(dv, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(dv, 0, 0);

    /* Clock section */
    lv_obj_t *cs = lv_obj_create(s_info_scr);
    lv_obj_remove_style_all(cs);
    lv_obj_set_size(cs, 750, 160);
    lv_obj_set_pos(cs, 25, 296);

    lv_obj_t *cb = lv_obj_create(cs);
    lv_obj_set_size(cb, 420, 120);
    lv_obj_set_pos(cb, 25, 20);
    lv_obj_set_style_bg_opa(cb, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cb, 0, 0);
    s_time = mk_lbl(cb, "--:--:--", C_TEXT, &font_clock_60);
    lv_obj_center(s_time);

    lv_obj_t *db = lv_obj_create(cs);
    lv_obj_set_size(db, 260, 120);
    lv_obj_set_pos(db, 470, 20);
    lv_obj_set_style_bg_opa(db, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(db, 0, 0);
    lv_obj_set_flex_flow(db, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(db, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_date = mk_lbl(db, "\u65F6\u95F4\u83B7\u53D6\u4E2D", C_MUTED, &font_wc_cn_20);
    s_nongli = mk_lbl(db, "", lv_color_hex(0xC8A050), &font_wc_cn_18);
    s_week = mk_lbl(db, "", C_MUTED, &font_wc_cn_16);
}

static void ui_tick(lv_timer_t *timer)
{
    (void)timer;
    if (s_scan_pending && !net_wifi_scan_busy()) {
        s_scan_pending = false;
        update_wifi_dd();
    }
    if (net_wifi_get_state() == NET_WIFI_CONNECTED && !s_svc) {
        s_svc = true;
        net_time_start();
        net_weather_start();
    }
    /* WiFi icon highlight */
    if (s_wifi_icon) {
        lv_color_t wc = (net_wifi_get_state() == NET_WIFI_CONNECTED)
            ? lv_color_hex(0x4FA8FF) : lv_color_hex(0x8A97A5);
        lv_obj_set_style_text_color(s_wifi_icon, wc, 0);
    }

    static int tc;
    tc++;
    if (tc % 16 == 0) {
        if (lv_obj_has_flag(s_tab_now, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_clear_flag(s_tab_now, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_tab_fc, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_tab_now, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(s_tab_fc, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (!net_time_is_synced()) return;
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);

    char b[48];
    strftime(b, sizeof(b), "%H:%M:%S", &tm);
    lv_label_set_text(s_time, b);

    snprintf(b, sizeof(b), "%d.%02d.%02d", tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday);
    lv_label_set_text(s_date, b);

    lunar_date_t ld = lunar_from_gregorian(tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday);
    char lb[32];
    lunar_format(lb, sizeof(lb), ld);
    lv_label_set_text(s_nongli, lb);

    snprintf(b, sizeof(b), "\u661F\u671F%s", WK[tm.tm_wday%7]);
    lv_label_set_text(s_week, b);

    /* Animated background */
    s_bg_tick++;
    static const char *last_cond = "";
    weather_info_t bw;
    const char *cond = (net_weather_get(&bw) && bw.valid) ? bw.cond : "";

    if (strcmp(cond, last_cond)) {
        last_cond = cond;
        lv_color_t top = lv_color_hex(0x0F2027), bot = lv_color_hex(0x2C5364);
        if (strstr(cond, "\u96F7"))      { top = lv_color_hex(0x141E30); bot = lv_color_hex(0x243B55); }
        else if (strstr(cond, "\u96E8")) { top = lv_color_hex(0x373B44); bot = lv_color_hex(0x4286F4); }
        else if (strstr(cond, "\u96EA")) { top = lv_color_hex(0xD3CCE3); bot = lv_color_hex(0xE9E4F0); }
        else if (strstr(cond, "\u6674")) { top = lv_color_hex(0x4FACFE); bot = lv_color_hex(0x00F2FE); }
        else if (strstr(cond, "\u591A\u4E91")) { top = lv_color_hex(0x8CA6DB); bot = lv_color_hex(0xB9935A); }
        else if (strstr(cond, "\u9634")) { top = lv_color_hex(0x606C88); bot = lv_color_hex(0x3F4C6B); }
        else if (strstr(cond, "\u96FE")||strstr(cond, "\u9708")) { top = lv_color_hex(0xCFD9DF); bot = lv_color_hex(0xE2EBF0); }
        lv_obj_set_style_bg_color(s_info_scr, top, 0);
        lv_obj_set_style_bg_grad_color(s_info_scr, bot, 0);
        lv_obj_set_style_bg_grad_dir(s_info_scr, LV_GRAD_DIR_VER, 0);
    }

    bool rain = strstr(cond, "\u96E8") || strstr(cond, "\u96F7");
    bool snow = strstr(cond, "\u96EA");
    bool cloudy = strstr(cond, "\u4E91") || strstr(cond, "\u9634");
    bool fog = strstr(cond, "\u96FE") || strstr(cond, "\u9708");
    bool thunder = strstr(cond, "\u96F7");

    /* Rain drops */
    for (int i = 0; i < 35; i++) {
        if (rain || thunder) {
            float y = (float)lv_obj_get_y(s_drops[i]) + s_drop_spd[i];
            if (y > 480) { y = -20.0f; lv_obj_set_pos(s_drops[i], (i * 23) % 780, (int)y); }
            lv_obj_set_pos(s_drops[i], lv_obj_get_x(s_drops[i]), (int)y);
            lv_obj_set_style_bg_opa(s_drops[i], thunder ? 50 + (i % 30) : 70 + (i % 25), 0);
        } else {
            lv_obj_set_style_bg_opa(s_drops[i], 0, 0);
        }
    }
    /* Snow flakes */
    for (int i = 0; i < 30; i++) {
        if (snow) {
            float y = (float)lv_obj_get_y(s_flakes[i]) + s_flake_spd[i];
            if (y > 480) { y = -10.0f; lv_obj_set_pos(s_flakes[i], (i * 27) % 780, (int)y); }
            int x = (int)((float)lv_obj_get_x(s_flakes[i]) + sinf((float)s_bg_tick * 0.05f + s_flake_ph[i]) * 1.5f);
            if (x < -10) { x = 800; } if (x > 810) { x = -10; }
            lv_obj_set_pos(s_flakes[i], x, (int)y);
            lv_obj_set_style_bg_opa(s_flakes[i], 80 + (i * 7) % 60, 0);
        } else {
            lv_obj_set_style_bg_opa(s_flakes[i], 0, 0);
        }
    }
    /* Cloud puffs */
    for (int i = 0; i < 5; i++) {
        if (cloudy) {
            float cx = (float)lv_obj_get_x(s_clouds[i][0]) + s_cloud_spd[i];
            if (cx > 880) cx = -120;
            int dx[4] = {0, 22, -18, 10}, dy[4] = {0, -8, 2, -12};
            for (int j = 0; j < 4; j++) {
                lv_obj_set_pos(s_clouds[i][j], (int)(cx + dx[j]), s_cloud_y[i] + dy[j]);
                lv_obj_set_style_bg_opa(s_clouds[i][j], 25 + j * 5, 0);
            }
        } else {
            for (int j = 0; j < 4; j++)
                lv_obj_set_style_bg_opa(s_clouds[i][j], 0, 0);
        }
    }
    /* Fog bands */
    for (int i = 0; i < 6; i++) {
        if (fog) {
            float sx = sinf((float)s_bg_tick * 0.02f + (float)i * 1.2f) * 120.0f;
            int x = (int)(-60.0f + sx);
            x = x % 440;
            if (x < -200) x += 640;
            lv_obj_set_pos(s_fogbands[i], x, 60 + i * 55);
            lv_obj_set_style_bg_opa(s_fogbands[i], 40, 0);
        } else {
            lv_obj_set_style_bg_opa(s_fogbands[i], 0, 0);
        }
    }
    /* Lightning */
    if (thunder) {
        static int flash_ctr;
        flash_ctr++;
        int opa = 0;
        if (flash_ctr % 70 == 0) opa = 100;
        else if (flash_ctr % 70 == 1) opa = 50;
        else if (flash_ctr % 70 == 2) opa = 20;
        lv_obj_set_style_bg_opa(s_flash, opa, 0);
    } else {
        lv_obj_set_style_bg_opa(s_flash, 0, 0);
    }

    weather_info_t w;
    if (net_weather_get(&w) && w.valid) {
        lv_label_set_text(s_wicon, wx_emoji(w.cond));
        lv_label_set_text(s_wname, w.cond);
        char tb[32]; strlcpy(tb, w.temp, sizeof(tb));
        char *dc = strstr(tb, "°C"); if (dc) { *dc = '\0'; strlcat(tb, "\u2103", sizeof(tb)); }
        lv_label_set_text(s_temp, tb);
        char hb[24];
        snprintf(hb, sizeof(hb), "%s", w.humidity);
        lv_label_set_text(s_humi, hb);
    }

    weather_forecast_t fc[3];
    if (net_weather_get_forecast(fc)) {
        static bool fc_ok;
        fc_ok = true;
        for (int i = 0; i < 3; i++) {
            lv_obj_t *c = s_fc[i];
            lv_obj_t *d = lv_obj_get_child(c, 0);
            lv_obj_t *fci = lv_obj_get_child(c, 1);
            lv_obj_t *n = lv_obj_get_child(c, 2);
            lv_obj_t *tp = lv_obj_get_child(c, 3);
            if (fc[i].valid) {
                lv_label_set_text(d, fc[i].date);
                lv_label_set_text(fci, wx_emoji(fc[i].cond));
                lv_label_set_text(n, fc[i].cond);
                lv_label_set_text(tp, fc[i].temp);
                char *fc_dc = strstr((char*)fc[i].temp, "°C");
                if (fc_dc) { *fc_dc = '\0'; strlcat((char*)fc[i].temp, "\u2103", sizeof(fc[i].temp)); }
                lv_label_set_text(tp, fc[i].temp);
            }
        }
    }
}

/* ========= WiFi screen ========= */
static void on_wifi_back(lv_event_t *e)
{
    (void)e;
    lv_screen_load(s_info_scr);
}

static void on_wifi_open(lv_event_t *e)
{
    (void)e;
    lv_screen_load(s_wifi_scr);
    s_scan_pending = false;
}

static void on_wifi_scan(lv_event_t *e)
{
    (void)e;
    net_wifi_scan_start();
    s_scan_pending = true;
    lv_label_set_text(s_wifi_status, "扫描中...");
}

static void update_wifi_dd(void)
{
    net_wifi_ap_t aps[16];
    int n = net_wifi_scan_get_aps(aps, 16);
    if (n <= 0) { lv_label_set_text(s_wifi_status, "未搜索到网络"); return; }
    static char opts[600];
    opts[0] = '\0';
    for (int i = 0; i < n; i++) {
        strlcat(opts, aps[i].ssid, sizeof(opts));
        if (i < n - 1) strlcat(opts, "\n", sizeof(opts));
    }
    lv_dropdown_set_options(s_ssid_dd, opts);
    lv_label_set_text(s_wifi_status, "请选择网络 输入密码");
}

static void on_wifi_connect(lv_event_t *e)
{
    (void)e;
    char ssid[33] = {0};
    lv_dropdown_get_selected_str(s_ssid_dd, ssid, sizeof(ssid));
    if (ssid[0] == '\0') return;
    const char *pass = lv_textarea_get_text(s_pass_ta);
    lv_label_set_text(s_wifi_status, "正在连接...");
    net_wifi_connect(ssid, pass, true);
}

static void build_wifi(void)
{
    s_wifi_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_wifi_scr, lv_color_hex(0x060D18), 0);
    lv_obj_clear_flag(s_wifi_scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back = lv_button_create(s_wifi_scr);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x4FA8FF), 0);
    lv_obj_set_style_radius(back, 10, 0);
    lv_obj_set_size(back, 80, 40);
    lv_obj_align(back, LV_ALIGN_TOP_LEFT, 16, 16);
    lv_obj_add_event_cb(back, on_wifi_back, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(back);
    lv_obj_set_style_text_font(bl, &font_wc_cn_24, 0);
    lv_obj_set_style_text_color(bl, lv_color_hex(0xF2F5F8), 0);
    lv_label_set_text(bl, "返回");
    lv_obj_center(bl);

    s_ssid_dd = lv_dropdown_create(s_wifi_scr);
    lv_dropdown_set_options(s_ssid_dd, "");
    lv_obj_set_pos(s_ssid_dd, 180, 60);
    lv_obj_set_size(s_ssid_dd, 400, 44);
    lv_dropdown_open(s_ssid_dd);
    lv_obj_t *dl = lv_dropdown_get_list(s_ssid_dd);
    if (dl) lv_obj_set_style_text_font(dl, &font_wc_cn_24, 0);
    lv_dropdown_close(s_ssid_dd);

    lv_obj_t *scan = lv_button_create(s_wifi_scr);
    lv_obj_set_style_bg_color(scan, lv_color_hex(0x4FA8FF), 0);
    lv_obj_set_style_radius(scan, 10, 0);
    lv_obj_set_size(scan, 80, 44);
    lv_obj_set_pos(scan, 600, 60);
    lv_obj_add_event_cb(scan, on_wifi_scan, LV_EVENT_CLICKED, NULL);
    lv_obj_t *sl = lv_label_create(scan);
    lv_obj_set_style_text_font(sl, &font_wc_cn_24, 0);
    lv_obj_set_style_text_color(sl, lv_color_hex(0xF2F5F8), 0);
    lv_label_set_text(sl, "扫描");
    lv_obj_center(sl);

    s_pass_ta = lv_textarea_create(s_wifi_scr);
    lv_textarea_set_one_line(s_pass_ta, true);
    lv_textarea_set_password_mode(s_pass_ta, true);
    lv_textarea_set_placeholder_text(s_pass_ta, "password");
    lv_obj_set_style_text_font(s_pass_ta, &lv_font_montserrat_24, 0);
    lv_obj_set_pos(s_pass_ta, 180, 130);
    lv_obj_set_size(s_pass_ta, 400, 44);

    lv_obj_t *conn = lv_button_create(s_wifi_scr);
    lv_obj_set_style_bg_color(conn, lv_color_hex(0x4FA8FF), 0);
    lv_obj_set_style_radius(conn, 10, 0);
    lv_obj_set_size(conn, 80, 44);
    lv_obj_set_pos(conn, 600, 130);
    lv_obj_add_event_cb(conn, on_wifi_connect, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cl = lv_label_create(conn);
    lv_obj_set_style_text_font(cl, &font_wc_cn_24, 0);
    lv_obj_set_style_text_color(cl, lv_color_hex(0xF2F5F8), 0);
    lv_label_set_text(cl, "连接");
    lv_obj_center(cl);

    s_wifi_status = mk_lbl(s_wifi_scr, "请扫描网络 输入密码", lv_color_hex(0x9AACC0), &font_wc_cn_24);
    lv_obj_set_pos(s_wifi_status, 180, 200);

    lv_obj_t *kb = lv_keyboard_create(s_wifi_scr);
    lv_obj_set_size(kb, 800, 220);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(kb, s_pass_ta);
}

void app_ui_create(void)
{
    build_wifi();
    build_info();
    lv_screen_load(s_info_scr);
    lv_timer_create(ui_tick, 500, NULL);
    ESP_LOGI(TAG, "UI ready");
}

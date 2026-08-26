/*
 * HTML-to-ESP Resource File
 * Auto-extracted from index.html — drop-in for ESP32 firmware
 *
 * Usage: #include "resources.h"
 *        lv_obj_set_style_bg_color(obj, WX_GRAD_SUNNY_TOP, 0);
 *        lv_label_set_text(label, WX_NAME_SUNNY);
 */

#pragma once
#include "lvgl.h"

/* ===== Weather Names (Chinese) ===== */
#define WX_NAME_SUNNY    "\xe6\x99\xb4"       /* 晴 */
#define WX_NAME_CLOUDY   "\xe5\xa4\x9a\xe4\xba\x91" /* 多云 */
#define WX_NAME_OVERCAST "\xe9\x98\xb4"       /* 阴 */
#define WX_NAME_RAIN     "\xe5\xb0\x8f\xe9\x9b\xa8" /* 小雨 */
#define WX_NAME_THUNDER  "\xe9\x9b\xb7\xe9\x9b\xa8" /* 雷雨 */
#define WX_NAME_SNOW     "\xe9\x9b\xaa"       /* 雪 */
#define WX_NAME_FOG      "\xe9\x9b\xbe"       /* 雾 */

static const char *WX_NAMES[] = {
    WX_NAME_SUNNY, WX_NAME_CLOUDY, WX_NAME_OVERCAST,
    WX_NAME_RAIN, WX_NAME_THUNDER, WX_NAME_SNOW, WX_NAME_FOG
};

/* ===== Weather Emoji Icons (UTF-8) ===== */
#define WX_EMO_SUNNY    "\xe2\x98\x80"        /* ☀ */
#define WX_EMO_CLOUDY   "\xe2\x9b\x85"        /* ⛅ */
#define WX_EMO_OVERCAST "\xe2\x98\x81"        /* ☁ */
#define WX_EMO_RAIN     "\xf0\x9f\x8c\xa7"    /* 🌧 */
#define WX_EMO_THUNDER  "\xe2\x9b\x88"        /* ⛈ */
#define WX_EMO_SNOW     "\xe2\x9d\x84"        /* ❄ */
#define WX_EMO_FOG      "\xf0\x9f\x8c\xab"    /* 🌫 */
#define WX_EMO_THERMO   "\xf0\x9f\x8c\xa1"    /* 🌡 */
#define WX_EMO_DROPLET  "\xf0\x9f\x92\xa7"    /* 💧 */

/* ===== Background Gradients (exact HTML CSS colors) ===== */
#define WX_GRAD_SUNNY_TOP    LV_COLOR_MAKE(0x12,0x5A,0xC8)
#define WX_GRAD_SUNNY_BOT    LV_COLOR_MAKE(0x86,0xC0,0xF0)
#define WX_GRAD_CLOUDY_TOP   LV_COLOR_MAKE(0x35,0x62,0x8F)
#define WX_GRAD_CLOUDY_BOT   LV_COLOR_MAKE(0x8F,0xB0,0xCE)
#define WX_GRAD_RAIN_TOP     LV_COLOR_MAKE(0x1A,0x24,0x30)
#define WX_GRAD_RAIN_BOT     LV_COLOR_MAKE(0x10,0x15,0x20)
#define WX_GRAD_SNOW_TOP     LV_COLOR_MAKE(0x1A,0x28,0x38)
#define WX_GRAD_SNOW_BOT     LV_COLOR_MAKE(0x0D,0x1A,0x2A)
#define WX_GRAD_FOG_TOP      LV_COLOR_MAKE(0x66,0x77,0x88)
#define WX_GRAD_FOG_BOT      LV_COLOR_MAKE(0x88,0x99,0xAA)
#define WX_GRAD_DEFAULT       LV_COLOR_MAKE(0x06,0x0D,0x18)

/* ===== Card Colors ===== */
#define COLOR_CARD_BG         LV_COLOR_MAKE(0x10,0x1E,0x34)
#define COLOR_TEXT_PRIMARY    LV_COLOR_MAKE(0xF2,0xF5,0xF8)
#define COLOR_TEXT_MUTED      LV_COLOR_MAKE(0x9A,0xAC,0xC0)
#define COLOR_ACCENT_BLUE     LV_COLOR_MAKE(0x4F,0xA8,0xFF)
#define COLOR_NONGLI_GOLD     LV_COLOR_MAKE(0xC8,0xA0,0x50)
#define COLOR_SCREEN_BG       LV_COLOR_MAKE(0x06,0x0D,0x18)
#define COLOR_WIFI_ON         LV_COLOR_MAKE(0x4F,0xA8,0xFF)
#define COLOR_WIFI_OFF        LV_COLOR_MAKE(0x8A,0x97,0xA5)

/* ===== Rain Drop Particles (matching HTML Canvas) ===== */
#define RAIN_COUNT       120   /* !< HTML uses 120 drops */
#define RAIN_MIN_SPEED   4.0f  /* !< px per frame */
#define RAIN_MAX_SPEED   12.0f
#define RAIN_MIN_ALPHA   0.3f  /* !< CSS rgba opacity */
#define RAIN_MAX_ALPHA   0.7f
#define RAIN_COLOR       LV_COLOR_MAKE(0xB4,0xD2,0xFF)

/* ===== Snow Flake Particles ===== */
#define SNOW_COUNT       80    /* !< HTML uses 80 flakes */
#define SNOW_MIN_SPEED   0.5f
#define SNOW_MAX_SPEED   2.0f
#define SNOW_MIN_RADIUS  2     /* !< px */
#define SNOW_MAX_RADIUS  6
#define SNOW_MIN_ALPHA   0.5f
#define SNOW_MAX_ALPHA   1.0f
#define SNOW_COLOR       LV_COLOR_MAKE(0xFF,0xFF,0xFF)
#define SNOW_WOBBLE_AMP  0.3f  /* !< sin(y*0.02) amplitude */

/* ===== Cloud Puff Particles ===== */
#define CLOUD_COUNT      8     /* !< HTML uses 8 clouds */
#define CLOUD_MIN_RADIUS 35    /* !< px */
#define CLOUD_MAX_RADIUS 75
#define CLOUD_MIN_SPEED  0.3f
#define CLOUD_MAX_SPEED  0.9f
#define CLOUD_MIN_ALPHA  0.4f
#define CLOUD_MAX_ALPHA  0.7f
#define CLOUD_COLOR      LV_COLOR_MAKE(0xFF,0xFF,0xFF)

/* ===== Fog Band Particles ===== */
#define FOG_COUNT        8     /* !< HTML uses 8 bands */
#define FOG_BAND_HEIGHT  30    /* !< px */
#define FOG_BAND_ALPHA   0.12f
#define FOG_BAND_COLOR   LV_COLOR_MAKE(0xFF,0xFF,0xFF)

/* ===== Thunder Lightning ===== */
#define LIGHTNING_ALPHA   0.4f
#define LIGHTNING_DECAY   0.85f
#define LIGHTNING_COLOR   LV_COLOR_MAKE(0xC8,0xC8,0xFF)

/* ===== Layout (exact px from HTML CSS) ===== */
#define LAYOUT_TAB_X      25
#define LAYOUT_TAB_Y      25
#define LAYOUT_TAB_W      750
#define LAYOUT_TAB_H      270
#define LAYOUT_CARD_W     700
#define LAYOUT_CARD_H     200
#define LAYOUT_CARD_X     50   /* 25+25 */
#define LAYOUT_FCARD_W    220
#define LAYOUT_FCARD_H    200
#define LAYOUT_FCARD_GAP  20
#define LAYOUT_CLOCK_W    420
#define LAYOUT_CLOCK_H    120
#define LAYOUT_DATE_W     260
#define LAYOUT_DATE_H     120
#define LAYOUT_DATE_GAP   25
#define LAYOUT_BOTTOM_Y   316  /* 25 + 271 + 20 */

/* ===== Font Sizes (exact px from HTML CSS) ===== */
#define FONT_WICON        100   /* weather icon emoji */
#define FONT_CLOCK         60   /* time */
#define FONT_FCICON        56   /* forecast icon */
#define FONT_TEMPVAL       32   /* temperature value */
#define FONT_WNAME         22   /* weather name / city */
#define FONT_DATE          20   /* date, forecast temp */
#define FONT_NONGLI        18   /* nongli, forecast name */
#define FONT_WEEK          16   /* week */

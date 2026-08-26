/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file bsp_buttons.c
 * @brief Debounced button scanning with short/long/very-long press events.
 *
 * Long/very-long thresholds come from Kconfig (LERO_BTN_LONG_MS /
 * LERO_BTN_VERY_LONG_MS). The 10 ms esp_timer callback only fires events; it
 * never blocks.
 */

#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "bsp_config.h"
#include "bsp_buttons.h"

#define TAG "bsp_buttons"

#define BTN_SCAN_PERIOD_US      10000U        /* 10 ms */
#define BTN_SHORT_MIN_MS        50U
#define BTN_DEBOUNCE_TICKS      2U

typedef struct {
    gpio_num_t gpio;
    bool stable_pressed;
    uint8_t debounce_cnt;
    bool pressed;
    uint64_t press_start_us;
    bool long_fired;
    bool very_long_fired;
} s_btn_state_t;

static s_btn_state_t s_buttons[BSP_BTN_ID_COUNT];
static bsp_buttons_cb_t s_handler;
static esp_timer_handle_t s_timer;

static const gpio_num_t s_btn_gpios[BSP_BTN_ID_COUNT] = {
    BSP_BTN1_GPIO,
    BSP_BTN2_GPIO,
    BSP_BTN3_GPIO,
};

static void s_emit(bsp_button_id_t id, bsp_button_event_t event)
{
    if (s_handler != NULL) {
        s_handler(id, event);
    }
}

static void s_timer_cb(void *arg)
{
    (void)arg;
    for (bsp_button_id_t id = BSP_BTN_ID_1; id < BSP_BTN_ID_COUNT; id = (bsp_button_id_t)((uint32_t)id + 1U)) {
        s_btn_state_t *st = &s_buttons[id];
        const int level = gpio_get_level(st->gpio);
        const bool raw_pressed = (level == BSP_BTN_ACTIVE_LEVEL);

        /* debounce */
        if (raw_pressed != st->stable_pressed) {
            st->debounce_cnt++;
            if (st->debounce_cnt >= BTN_DEBOUNCE_TICKS) {
                st->stable_pressed = raw_pressed;
                st->debounce_cnt = 0U;
            }
        } else {
            st->debounce_cnt = 0U;
        }

        if (st->stable_pressed) {
            if (!st->pressed) {
                st->pressed = true;
                st->press_start_us = esp_timer_get_time();
                st->long_fired = false;
                st->very_long_fired = false;
            } else {
                const uint32_t dur = (uint32_t)((esp_timer_get_time() - st->press_start_us) / 1000U);
                if (!st->very_long_fired && (dur >= (uint32_t)CONFIG_LERO_BTN_VERY_LONG_MS)) {
                    st->very_long_fired = true;
                    s_emit(id, BSP_BTN_EVENT_VERY_LONG_PRESS);
                } else if (!st->long_fired && (dur >= (uint32_t)CONFIG_LERO_BTN_LONG_MS)) {
                    st->long_fired = true;
                    s_emit(id, BSP_BTN_EVENT_LONG_PRESS);
                }
            }
        } else if (st->pressed) {
            const uint32_t dur = (uint32_t)((esp_timer_get_time() - st->press_start_us) / 1000U);
            st->pressed = false;
            if ((dur >= BTN_SHORT_MIN_MS) && (dur < (uint32_t)CONFIG_LERO_BTN_LONG_MS)) {
                s_emit(id, BSP_BTN_EVENT_SHORT_PRESS);
            }
        }
    }
}

esp_err_t bsp_buttons_init(void)
{
    uint64_t pin_mask = 0U;
    for (bsp_button_id_t id = BSP_BTN_ID_1; id < BSP_BTN_ID_COUNT; id = (bsp_button_id_t)((uint32_t)id + 1U)) {
        s_buttons[id].gpio = s_btn_gpios[id];
        pin_mask |= (1ULL << (uint64_t)s_btn_gpios[id]);
    }

    gpio_config_t cfg = {
        .pin_bit_mask = pin_mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio config failed: %s", esp_err_to_name(err));
        return err;
    }

    if (s_timer == NULL) {
        esp_timer_create_args_t args = {
            .callback = s_timer_cb,
            .arg = NULL,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "bsp_btn",
            .skip_unhandled_events = false,
        };
        err = esp_timer_create(&args, &s_timer);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "timer create failed: %s", esp_err_to_name(err));
            return err;
        }
        err = esp_timer_start_periodic(s_timer, BTN_SCAN_PERIOD_US);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "timer start failed: %s", esp_err_to_name(err));
            return err;
        }
    }
    ESP_LOGI(TAG, "buttons ready (long=%dms very_long=%dms)",
             CONFIG_LERO_BTN_LONG_MS, CONFIG_LERO_BTN_VERY_LONG_MS);
    return ESP_OK;
}

esp_err_t bsp_buttons_set_handler(bsp_buttons_cb_t cb)
{
    s_handler = cb;
    return ESP_OK;
}


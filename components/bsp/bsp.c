/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file bsp.c
 * @brief BSP entry: initializes all enabled modules in dependency order.
 *
 * Order (docs/PLAN.md 3.3): buttons / power / usb / amplifier (no deps),
 * then I2C buses, then display / touch / codec / imu, then sd / storage.
 * Partial failure is allowed; the fault bitmap is exposed to diag.
 */

#include <string.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_app_desc.h"
#include "bsp.h"
#include "bsp_i2c.h"
#include "bsp_buttons.h"
#include "bsp_power.h"
#include "bsp_usb.h"
#include "bsp_amplifier.h"
#include "bsp_sdcard.h"
#include "bsp_storage.h"
#include "bsp_imu.h"
#include "bsp_codec.h"
#include "bsp_display.h"
#include "bsp_touch.h"

#define TAG "bsp"

typedef struct {
    bsp_module_t id;
    const char *name;
    esp_err_t (*init_fn)(void);
} bsp_module_desc_t;

static bsp_module_status_t s_status[BSP_MODULE_COUNT];
static bool s_initialized;

static void s_status_init_all(void)
{
    for (bsp_module_t m = BSP_MODULE_BUTTONS; m < BSP_MODULE_COUNT; m = (bsp_module_t)((uint32_t)m + 1U)) {
        bsp_module_status_t *st = &s_status[m];
        (void)memset(st, 0, sizeof(*st));
        st->enabled = false;
        st->init_ok = false;
        st->last_error = ESP_ERR_NOT_SUPPORTED;
    }
}

static void s_status_update(const bsp_module_desc_t *desc, bool ok, esp_err_t err)
{
    bsp_module_status_t *st = &s_status[desc->id];
    (void)strlcpy(st->name, desc->name, sizeof(st->name));
    st->enabled = true;
    st->init_ok = ok;
    st->last_error = err;
}

esp_err_t bsp_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    s_status_init_all();

#if CONFIG_LERO_BSP_ENABLE_CODEC || CONFIG_LERO_BSP_ENABLE_IMU || CONFIG_LERO_BSP_ENABLE_TOUCH
    if (bsp_i2c_init() != ESP_OK) {
        ESP_LOGW(TAG, "I2C buses init failed; codec/imu/touch will report errors");
    }
#endif

    static const bsp_module_desc_t s_modules[] = {
#if CONFIG_LERO_BSP_ENABLE_BUTTONS
        { BSP_MODULE_BUTTONS, "buttons", bsp_buttons_init },
#endif
#if CONFIG_LERO_BSP_ENABLE_POWER
        { BSP_MODULE_POWER, "power", bsp_power_init },
#endif
#if CONFIG_LERO_BSP_ENABLE_USB
        { BSP_MODULE_USB, "usb", bsp_usb_init },
#endif
#if CONFIG_LERO_BSP_ENABLE_AMPLIFIER
        { BSP_MODULE_AMPLIFIER, "amplifier", bsp_amp_init },
#endif
#if CONFIG_LERO_BSP_ENABLE_DISPLAY
        { BSP_MODULE_DISPLAY, "display", bsp_display_init },
#endif
#if CONFIG_LERO_BSP_ENABLE_TOUCH
        { BSP_MODULE_TOUCH, "touch", bsp_touch_init },
#endif
#if CONFIG_LERO_BSP_ENABLE_CODEC
        { BSP_MODULE_CODEC, "codec", bsp_codec_init },
#endif
#if CONFIG_LERO_BSP_ENABLE_IMU
        { BSP_MODULE_IMU, "imu", bsp_imu_init },
#endif
#if CONFIG_LERO_BSP_ENABLE_SDCARD
        { BSP_MODULE_SDCARD, "sdcard", bsp_sdcard_init },
#endif
#if CONFIG_LERO_BSP_ENABLE_STORAGE
        { BSP_MODULE_STORAGE, "storage", bsp_storage_init },
#endif
    };

    for (size_t i = 0U; i < (sizeof(s_modules) / sizeof(s_modules[0])); i++) {
        const bsp_module_desc_t *desc = &s_modules[i];
        esp_err_t err = desc->init_fn();
        bool ok = (err == ESP_OK);
        s_status_update(desc, ok, err);
        if (!ok) {
            ESP_LOGW(TAG, "module %s init failed: %s", desc->name, esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "module %s init ok", desc->name);
        }
    }

    s_initialized = true;
    ESP_LOGI(TAG, "bsp ready, version %s", bsp_get_version());
    return ESP_OK;
}

esp_err_t bsp_deinit(void)
{
    if (!s_initialized) {
        return ESP_OK;
    }
    (void)bsp_storage_unmount();
    (void)bsp_sdcard_unmount();
    (void)bsp_i2c_deinit();
    s_initialized = false;
    return ESP_OK;
}

esp_err_t bsp_get_module_status(bsp_module_t module, bsp_module_status_t *status)
{
    if ((status == NULL) || ((uint32_t)module >= (uint32_t)BSP_MODULE_COUNT)) {
        return ESP_ERR_INVALID_ARG;
    }
    *status = s_status[module];
    return ESP_OK;
}

uint32_t bsp_get_fault_bitmap(void)
{
    uint32_t bitmap = 0U;
    for (bsp_module_t m = BSP_MODULE_BUTTONS; m < BSP_MODULE_COUNT; m = (bsp_module_t)((uint32_t)m + 1U)) {
        if (s_status[m].enabled && !s_status[m].init_ok) {
            bitmap |= (1UL << (uint32_t)m);
        }
    }
    return bitmap;
}

const char *bsp_get_version(void)
{
    const esp_app_desc_t *desc = esp_app_get_description();
    return (desc != NULL) ? desc->version : "unknown";
}


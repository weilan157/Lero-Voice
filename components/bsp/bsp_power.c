/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file bsp_power.c
 * @brief ADC power monitoring + status LED.
 */

#include "sdkconfig.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "bsp_config.h"
#include "bsp_power.h"

#define TAG "bsp_power"

#define ADC_RAW_MAX         4095U
#define ADC_REF_MV          3300U
#define CHARGER_VBUS_MV     4500U
#define BAT_FULL_MV         4200U
#define BAT_EMPTY_MV        3000U

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;
static bsp_power_sleep_hook_t s_hook_before;
static bsp_power_sleep_hook_t s_hook_after;

static esp_err_t s_adc_read_channel(adc_channel_t channel, uint32_t *mv)
{
    int raw = 0;
    esp_err_t err = adc_oneshot_read(s_adc, channel, &raw);
    if (err != ESP_OK) {
        return err;
    }
    if (raw < 0) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (s_cali != NULL) {
        int voltage = 0;
        err = adc_cali_raw_to_voltage(s_cali, raw, &voltage);
        if (err != ESP_OK) {
            return err;
        }
        *mv = (voltage > 0) ? (uint32_t)voltage : 0U;
    } else {
        *mv = ((uint32_t)raw * ADC_REF_MV) / ADC_RAW_MAX;
    }
    return ESP_OK;
}

esp_err_t bsp_power_init(void)
{
    /* S31 ADC 映射（soc/esp32s31/adc_channel.h）：
     *   ADC1: GPIO42~49 = CH0~7
     *   ADC2: GPIO50~57 = CH0~7（BAT_ADC=IO50→ADC2_CH0, BUS_ADC=IO51→ADC2_CH1）
     * S31 ADC 为新一代 SAR（17-bit、支持差分）：SOC_ADC_ATTEN_NUM=1，
     * 仅 ADC_ATTEN_DB_0 合法（DB_11 在 S31 编译期不存在、DB_12 运行时报
     * invalid attenuation，见官方 examples/peripherals/adc/oneshot_read）。
     * 注意：ADC2 与 Wi-Fi 的共存性需上板实测（部分芯片 ADC2 在 Wi-Fi 活跃时
     * 读数不可用；若受影响需改用 ADC1 引脚或连续采样模式）。 */
#define ADC_MONITOR_UNIT  ADC_UNIT_2
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_MONITOR_UNIT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    esp_err_t err = adc_oneshot_new_unit(&unit_cfg, &s_adc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc unit create failed: %s", esp_err_to_name(err));
        return err;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_0,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_oneshot_config_channel(s_adc, (adc_channel_t)CONFIG_LERO_BAT_ADC_CHANNEL, &chan_cfg);
    if (err == ESP_OK) {
        err = adc_oneshot_config_channel(s_adc, (adc_channel_t)CONFIG_LERO_BUS_ADC_CHANNEL, &chan_cfg);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc channel config failed: %s", esp_err_to_name(err));
        return err;
    }

    /* 曲线拟合校准。注意：当前目标 esp32s31 暂未启用
     * ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED（见 esp_adc 的 adc_cali_schemes.h，
     * TODO: 待 S31 支持后启用），此时相关类型/函数在编译期不存在，
     * 因此仅在支持该方案的芯片上启用，否则直接走近似换算。 */
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = ADC_MONITOR_UNIT,
        .chan = (adc_channel_t)CONFIG_LERO_BAT_ADC_CHANNEL,
        .atten = ADC_ATTEN_DB_0,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "adc cali unavailable (%s); use raw approximation",
                 esp_err_to_name(err));
        s_cali = NULL;
    }
#else
    ESP_LOGW(TAG, "ADC curve fitting cali not supported on this target; use raw approximation");
    s_cali = NULL;
#endif

    gpio_config_t led_cfg = {
        .pin_bit_mask = (1ULL << (uint64_t)BSP_LED_STATUS_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    err = gpio_config(&led_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "led gpio config failed: %s", esp_err_to_name(err));
        return err;
    }
    (void)gpio_set_level(BSP_LED_STATUS_GPIO, 1);   /* 默认熄灭（低电平点亮） */

    ESP_LOGI(TAG, "power ready (ADC2 ch%d/ch%d, atten=DB0, cali=%s)",
             (int)CONFIG_LERO_BAT_ADC_CHANNEL, (int)CONFIG_LERO_BUS_ADC_CHANNEL,
             (s_cali != NULL) ? "yes" : "no");
    return ESP_OK;
}

esp_err_t bsp_power_get_battery_mv(uint32_t *mv)
{
    if (mv == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = s_adc_read_channel((adc_channel_t)CONFIG_LERO_BAT_ADC_CHANNEL, mv);
    if (err == ESP_OK) {
        *mv = (*mv * (uint32_t)CONFIG_LERO_BAT_DIVIDER_RATIO) / 100U;
    }
    return err;
}

esp_err_t bsp_power_get_bus_mv(uint32_t *mv)
{
    if (mv == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = s_adc_read_channel((adc_channel_t)CONFIG_LERO_BUS_ADC_CHANNEL, mv);
    if (err == ESP_OK) {
        *mv = (*mv * (uint32_t)CONFIG_LERO_BUS_DIVIDER_RATIO) / 100U;
    }
    return err;
}

esp_err_t bsp_power_get_battery_pct(uint8_t *pct)
{
    if (pct == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    uint32_t mv = 0U;
    esp_err_t err = bsp_power_get_battery_mv(&mv);
    if (err != ESP_OK) {
        return err;
    }
    if (mv >= BAT_FULL_MV) {
        *pct = 100U;
    } else if (mv <= BAT_EMPTY_MV) {
        *pct = 0U;
    } else {
        *pct = (uint8_t)(((mv - BAT_EMPTY_MV) * 100U) / (BAT_FULL_MV - BAT_EMPTY_MV));
    }
    return ESP_OK;
}

esp_err_t bsp_power_get_charge_state(bool *charging)
{
    if (charging == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    uint32_t bus_mv = 0U;
    esp_err_t err = bsp_power_get_bus_mv(&bus_mv);
    if (err != ESP_OK) {
        return err;
    }
    *charging = (bus_mv >= CHARGER_VBUS_MV);
    return ESP_OK;
}

esp_err_t bsp_power_set_led(bool on)
{
    return gpio_set_level(BSP_LED_STATUS_GPIO, on ? BSP_LED_ACTIVE_LEVEL : (1 - BSP_LED_ACTIVE_LEVEL));
}

esp_err_t bsp_power_set_sleep_hooks(bsp_power_sleep_hook_t before, bsp_power_sleep_hook_t after)
{
    s_hook_before = before;
    s_hook_after = after;
    return ESP_OK;
}


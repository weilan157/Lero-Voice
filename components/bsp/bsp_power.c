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

/* S31 新一代 SAR ADC：17-bit、DB_0 满量程 2000 mV（官方 esp32_s31_adc_calibration.c
 * 模型：BSP_S31_ADC_BIT_COUNT=17、MAX_MV=2000、raw 掩码 0x1FFFF）。
 * S31 不支持 curve fitting 校准（ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED 为假），
 * 且官方 regi2c 软件校准仅支持 ADC1（本板 BAT/BUS 在 ADC2）——
 * 用官方"理想 bit 权重"模型做近似换算（误差极小，绝对值 ≤ 数十 mV）。 */
#define ADC_RAW_MASK        0x1FFFFU
#define ADC_MAX_MV          2000
#define ADC_BIT_COUNT       17
#define CHARGER_VBUS_MV     4500U
#define BAT_FULL_MV         4200U
#define BAT_EMPTY_MV        3000U

/* 官方理想 bit 权重（Q8 定点；esp32_s31_adc_calibration.c s_ideal_weights） */
static const int32_t s_adc_ideal_weights[ADC_BIT_COUNT] = {
    2048, 1024, 512, 256, 256, 128, 64, 32, 32, 16, 8, 8, 4, 2, 2, 0, 1,
};

static uint32_t s_adc_raw_to_mv(int raw)
{
    const uint32_t r = (uint32_t)raw & ADC_RAW_MASK;
    int32_t code_q = 0;
    for (int i = 0; i < ADC_BIT_COUNT; i++) {
        if ((r & (1U << (ADC_BIT_COUNT - 1 - i))) != 0U) {
            code_q += s_adc_ideal_weights[i];
        }
    }
    int64_t mv = (int64_t)ADC_MAX_MV -
                 ((int64_t)4000 * code_q) / (4393 * 256);
    if (mv < 0) {
        mv = 0;
    } else if (mv > ADC_MAX_MV) {
        mv = ADC_MAX_MV;
    }
    return (uint32_t)mv;
}

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
        /* S31：17-bit / DB_0 满量程 2000 mV 理想权重换算（见 s_adc_raw_to_mv） */
        *mv = s_adc_raw_to_mv(raw);
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
    /* 充电判定：总线电压达到充电器阈值，或 ADC 已饱和
     * （DB_0 满量程 2000 mV；1:2 分压下 4.0V 即饱和——
     * 饱和说明 VBUS 高于分压量程，必然有充电器接入） */
    const uint32_t bus_saturated =
        ((uint32_t)ADC_MAX_MV * (uint32_t)CONFIG_LERO_BUS_DIVIDER_RATIO) / 100U;
    *charging = (bus_mv >= CHARGER_VBUS_MV) || (bus_mv >= bus_saturated);
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


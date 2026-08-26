/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file bsp_power.h
 * @brief Power monitoring: BAT_ADC(IO50) / BUS_ADC(IO51) / status LED(IO49).
 *
 * The charger (LGS5500EP) has no I2C; charge state is inferred from BUS
 * voltage (docs/PLAN.md 3.3.1 #5).
 */

#ifndef BSP_POWER_H
#define BSP_POWER_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*bsp_power_sleep_hook_t)(void);

/**
 * @brief Init ADC (oneshot + calibration) and the status LED GPIO.
 * @return ESP_OK on success.
 */
esp_err_t bsp_power_init(void);

/**
 * @brief Read battery voltage in mV.
 * @param[out] mv Battery voltage.
 * @return ESP_OK / error.
 */
esp_err_t bsp_power_get_battery_mv(uint32_t *mv);

/**
 * @brief Read bus (VBUS) voltage in mV.
 * @param[out] mv Bus voltage.
 * @return ESP_OK / error.
 */
esp_err_t bsp_power_get_bus_mv(uint32_t *mv);

/**
 * @brief Approximate battery level in percent (3.0 V .. 4.2 V map).
 * @param[out] pct Battery level [0..100].
 * @return ESP_OK / error.
 */
esp_err_t bsp_power_get_battery_pct(uint8_t *pct);

/**
 * @brief Infer charging state from BUS voltage (>= 4.5 V => charger present).
 * @param[out] charging true = charging / external power present.
 * @return ESP_OK / error.
 */
esp_err_t bsp_power_get_charge_state(bool *charging);

/**
 * @brief Set the status LED (IO49, active low).
 * @param[in] on true = LED on.
 * @return ESP_OK on success.
 */
esp_err_t bsp_power_set_led(bool on);

/**
 * @brief Register sleep hooks (called by the system power orchestration).
 * @param[in] before Hook before entering sleep (may be NULL).
 * @param[in] after  Hook after waking up (may be NULL).
 * @return ESP_OK on success.
 */
esp_err_t bsp_power_set_sleep_hooks(bsp_power_sleep_hook_t before, bsp_power_sleep_hook_t after);

#ifdef __cplusplus
}
#endif

#endif /* BSP_POWER_H */


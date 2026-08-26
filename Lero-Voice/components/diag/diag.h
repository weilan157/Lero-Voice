/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file diag.h
 * @brief Unified diagnostics (docs/PLAN.md 3.8).
 *
 * Channels: UART console (esp_console REPL), ring log to SD, RAM last-words,
 * fault bitmap + reset reason, periodic snapshot. The UI diagnostics page can
 * consume the same data sources later.
 */

#ifndef DIAG_H
#define DIAG_H

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DIAG_MODULE_NAME_MAX   16U
#define DIAG_MAX_MODULES       12U

typedef esp_err_t (*diag_status_fn_t)(char *out, size_t len);

typedef struct {
    char name[DIAG_MODULE_NAME_MAX];    /*< 模块名 */
    diag_status_fn_t status_fn;         /*< 输出一行状态文本（含换行） */
} diag_module_t;

/**
 * @brief Initialize diagnostics: log hook, reset reason, snapshot task,
 *        console REPL (when enabled).
 * @return ESP_OK on success.
 */
esp_err_t diag_init(void);

/**
 * @brief Register a module for the diagnostics page.
 * @param[in] module Module descriptor (copied).
 * @return ESP_OK / ESP_ERR_NO_MEM when the registry is full.
 */
esp_err_t diag_register_module(const diag_module_t *module);

/**
 * @brief Concatenate all registered module status lines.
 * @param[out] out Output buffer.
 * @param[in]  len Buffer size.
 * @return ESP_OK on success.
 */
esp_err_t diag_get_modules_text(char *out, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* DIAG_H */


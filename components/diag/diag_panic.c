/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file diag_panic.c
 * @brief Crash diagnostics: coredump partition + last-words + reset reason.
 *
 * The coredump partition is configured in partitions.csv with
 * CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH (docs/PLAN.md 3.8.5); offline analysis
 * via "idf.py coredump-info".
 *
 * NOTE: IDF 6.x removed the public esp_set_panic_handler() hook API; the
 * last-words ring (diag_log) plus the coredump cover crash forensics today.
 * A custom panic hook can be re-added when the new API is public.
 */

#include "esp_log.h"
#include "diag_internal.h"

#define TAG "diag_panic"

esp_err_t diag_panic_init(void)
{
    ESP_LOGI(TAG, "panic diagnostics ready (coredump partition + last-words)");
    return ESP_OK;
}


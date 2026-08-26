/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file diag_internal.h
 * @brief Internal interface shared by the diag modules.
 */

#ifndef DIAG_INTERNAL_H
#define DIAG_INTERNAL_H

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

/* diag_log.c */
esp_err_t diag_log_init(void);
esp_err_t diag_log_flush(void);
const char *diag_log_get_last_words(void);

/* diag_errors.c */
esp_err_t diag_errors_init(void);
void diag_errors_update_faults(uint32_t faults);
uint32_t diag_errors_get_faults(void);
uint32_t diag_errors_get_crash_count(void);
const char *diag_errors_get_reset_reason_str(void);

/* diag_panic.c */
esp_err_t diag_panic_init(void);

/* diag_snapshot.c */
esp_err_t diag_snapshot_init(void);
esp_err_t diag_snapshot_capture(void);
esp_err_t diag_snapshot_get_latest(char *out, size_t len);

/* diag_console.c */
esp_err_t diag_console_register(void);
esp_err_t diag_console_start(void);

#endif /* DIAG_INTERNAL_H */


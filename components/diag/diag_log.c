/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file diag_log.c
 * @brief Log pipeline: vprintf hook -> UART + RAM last-words ring + optional
 *        rotating SD mirror (/logs/lero_N.log, docs/PLAN.md 3.8.4).
 *
 * SD writes are buffered (1 KB) and flushed periodically by diag_task; a
 * write failure degrades to RAM-only logging and never blocks the caller.
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "bsp_sdcard.h"
#include "diag_internal.h"

#define TAG "diag_log"

#define LAST_WORDS_SIZE     4096U
#define PENDING_FLUSH_SIZE  1024U
#define LOG_LINE_MAX        192U
#define LOG_DIR             "/sdcard/logs"
#define LOG_FILE_PREFIX     "lero_"

static char s_last_words[LAST_WORDS_SIZE];
static size_t s_last_words_len;
static char s_pending[PENDING_FLUSH_SIZE];
static size_t s_pending_len;
static uint8_t s_log_file_index;

static void s_ring_append(const char *data, size_t len)
{
    if (len >= LAST_WORDS_SIZE) {
        (void)memcpy(s_last_words, data + (len - LAST_WORDS_SIZE), LAST_WORDS_SIZE);
        s_last_words_len = LAST_WORDS_SIZE;
        return;
    }
    if ((s_last_words_len + len) <= LAST_WORDS_SIZE) {
        (void)memcpy(&s_last_words[s_last_words_len], data, len);
        s_last_words_len += len;
        return;
    }
    const size_t keep = LAST_WORDS_SIZE - len;
    (void)memmove(s_last_words, &s_last_words[s_last_words_len - keep], keep);
    (void)memcpy(&s_last_words[keep], data, len);
    s_last_words_len = LAST_WORDS_SIZE;
}

static void s_pending_append(const char *data, size_t len)
{
    if ((s_pending_len + len) <= PENDING_FLUSH_SIZE) {
        (void)memcpy(&s_pending[s_pending_len], data, len);
        s_pending_len += len;
    }
}

static int s_vprintf_hook(const char *fmt, va_list args)
{
    char line[LOG_LINE_MAX];
    int n = vsnprintf(line, sizeof(line), fmt, args);
    if (n < 0) {
        n = 0;
    }
    if ((size_t)n >= sizeof(line)) {
        n = (int)sizeof(line) - 1;
    }
    /* 输出到串口（vfs 写，不会递归回 vprintf） */
    (void)fwrite(line, 1U, (size_t)n, stdout);
    s_ring_append(line, (size_t)n);
    s_pending_append(line, (size_t)n);
    return n;
}

esp_err_t diag_log_init(void)
{
    (void)esp_log_set_vprintf(s_vprintf_hook);
    s_last_words_len = 0U;
    s_pending_len = 0U;
    s_log_file_index = 0U;
    ESP_LOGI(TAG, "log hook ready (last-words %d B)", (int)LAST_WORDS_SIZE);
    return ESP_OK;
}

const char *diag_log_get_last_words(void)
{
    static const char s_empty[] = "(no log)";
    return (s_last_words_len > 0U) ? s_last_words : s_empty;
}

static void s_write_pending_to_file(const char *path)
{
    FILE *f = fopen(path, "a");
    if (f == NULL) {
        return;
    }
    (void)fseek(f, 0L, SEEK_END);
    const long size = ftell(f);
    if ((size >= 0L) && ((unsigned long)size >=
                         ((unsigned long)CONFIG_LERO_DIAG_LOG_FILE_SIZE_KB * 1024U))) {
        (void)fclose(f);
        s_log_file_index = (uint8_t)((s_log_file_index + 1U) %
                                     (uint8_t)CONFIG_LERO_DIAG_LOG_FILES);
        char next_path[128];
        (void)snprintf(next_path, sizeof(next_path), "%s/%s%u.log",
                       LOG_DIR, LOG_FILE_PREFIX, (unsigned)s_log_file_index);
        f = fopen(next_path, "w");
        if (f == NULL) {
            return;
        }
    }
    (void)fwrite(s_pending, 1U, s_pending_len, f);
    (void)fclose(f);
    s_pending_len = 0U;
}

esp_err_t diag_log_flush(void)
{
#if CONFIG_LERO_DIAG_SD_LOG
    if (s_pending_len == 0U) {
        return ESP_OK;
    }
    if (!bsp_sdcard_is_mounted()) {
        return ESP_ERR_INVALID_STATE;   /* 保留 pending，等待下次 */
    }
    char path[128];
    (void)snprintf(path, sizeof(path), "%s/%s%u.log",
                   LOG_DIR, LOG_FILE_PREFIX, (unsigned)s_log_file_index);
    s_write_pending_to_file(path);
#endif
    return ESP_OK;
}


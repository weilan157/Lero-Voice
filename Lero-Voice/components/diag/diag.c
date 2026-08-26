/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file diag.c
 * @brief Diagnostics entry: log hook, errors, snapshot task, console REPL.
 *
 * diag_task (priority 1, Core 0, docs/PLAN.md 3.5.1) periodically refreshes
 * the fault bitmap, captures a snapshot and flushes the SD log mirror.
 */

#include <string.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bsp.h"
#include "diag.h"
#include "diag_internal.h"

#define TAG "diag"

static StackType_t s_diag_stack[CONFIG_LERO_DIAG_TASK_STACK_SIZE / sizeof(StackType_t)];
static StaticTask_t s_diag_tcb;
static diag_module_t s_modules[DIAG_MAX_MODULES];
static uint8_t s_module_count;

static void s_diag_task(void *arg)
{
    (void)arg;
    for (;;) {
        diag_errors_update_faults(bsp_get_fault_bitmap());
        (void)diag_snapshot_capture();
        (void)diag_log_flush();
        vTaskDelay(pdMS_TO_TICKS(CONFIG_LERO_DIAG_SNAPSHOT_PERIOD_MS));
    }
}

esp_err_t diag_init(void)
{
    (void)diag_log_init();
    (void)diag_errors_init();
    (void)diag_panic_init();
    (void)diag_snapshot_init();

#if CONFIG_LERO_DIAG_CONSOLE
    (void)diag_console_register();
    (void)diag_console_start();     /* REPL 运行在框架内部任务中 */
#endif

    xTaskCreateStaticPinnedToCore(s_diag_task, "diag_task", sizeof(s_diag_stack), NULL,
                                  CONFIG_LERO_DIAG_TASK_PRIORITY,
                                  s_diag_stack, &s_diag_tcb, CONFIG_LERO_DIAG_TASK_CORE);
    ESP_LOGI(TAG, "diag ready (console=%d sd_log=%d snapshot=%d ms)",
             CONFIG_LERO_DIAG_CONSOLE, CONFIG_LERO_DIAG_SD_LOG,
             CONFIG_LERO_DIAG_SNAPSHOT_PERIOD_MS);
    return ESP_OK;
}

esp_err_t diag_register_module(const diag_module_t *module)
{
    if (module == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_module_count >= DIAG_MAX_MODULES) {
        return ESP_ERR_NO_MEM;
    }
    (void)strlcpy(s_modules[s_module_count].name, module->name,
                  sizeof(s_modules[s_module_count].name));
    s_modules[s_module_count].status_fn = module->status_fn;
    s_module_count++;
    return ESP_OK;
}

esp_err_t diag_get_modules_text(char *out, size_t len)
{
    if ((out == NULL) || (len == 0U)) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t used = 0U;
    out[0] = '\0';
    for (uint8_t i = 0U; i < s_module_count; i++) {
        if (s_modules[i].status_fn == NULL) {
            continue;
        }
        const size_t remain = len - used;
        if (remain < 8U) {
            break;
        }
        char line[128];
        (void)s_modules[i].status_fn(line, sizeof(line));
        const size_t l = strlen(line);
        const size_t copy = (l < remain) ? l : (remain - 1U);
        (void)memcpy(&out[used], line, copy);
        used += copy;
        out[used] = '\0';
    }
    return ESP_OK;
}


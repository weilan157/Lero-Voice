/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file diag_console.c
 * @brief esp_console REPL commands (docs/PLAN.md 3.8.3).
 *
 * All commands only READ state (no config mutation) except "log" which
 * changes the runtime log level. Sensitive data (passwords / tokens / full
 * NVS values) is never printed; MAC addresses are masked.
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <dirent.h>
#include "sdkconfig.h"
#include "esp_console.h"
#include "esp_stdio_cli_config.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bsp.h"
#include "bsp_power.h"
#include "bsp_sdcard.h"
#include "provisioning.h"
#include "ota_service.h"
#include "diag.h"
#include "diag_internal.h"

#define TAG "diag_console"

static int cmd_version(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    const esp_app_desc_t *desc = esp_app_get_description();
    printf("Lero Voice %s\n", bsp_get_version());
    printf("IDF: %s\n", IDF_VER);
    printf("Target: %s\n", CONFIG_IDF_TARGET);
    if (desc != NULL) {
        printf("Build: %s %s\n", desc->date, desc->time);
    }
    return 0;
}

static int cmd_mem(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("Free heap: %" PRIu32 " B\n", (uint32_t)esp_get_free_heap_size());
    printf("Min free heap: %" PRIu32 " B\n", (uint32_t)esp_get_minimum_free_heap_size());
    printf("Free internal: %" PRIu32 " B\n", (uint32_t)esp_get_free_internal_heap_size());
    printf("Free SPIRAM: %" PRIu32 " B\n",
           (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    return 0;
}

static int cmd_tasks(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    static TaskStatus_t s_statuses[32];
    uint32_t total = 0U;
    const uint32_t count = uxTaskGetSystemState(s_statuses, 32U, &total);
    for (uint32_t i = 0U; i < count; i++) {
        printf("%-16s prio=%lu hwm=%lu\n",
               s_statuses[i].pcTaskName,
               (unsigned long)s_statuses[i].uxCurrentPriority,
               (unsigned long)s_statuses[i].usStackHighWaterMark);
    }
    printf("total tasks: %lu\n", (unsigned long)total);
    return 0;
}

static int cmd_periph(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    for (bsp_module_t m = BSP_MODULE_BUTTONS; m < BSP_MODULE_COUNT;
         m = (bsp_module_t)((uint32_t)m + 1U)) {
        bsp_module_status_t st;
        if (bsp_get_module_status(m, &st) != ESP_OK) {
            continue;
        }
        if (!st.enabled) {
            printf("[%-8s] disabled\n", st.name);
        } else if (st.init_ok) {
            printf("[%-8s] ok\n", st.name);
        } else {
            printf("[%-8s] FAIL: %s\n", st.name, esp_err_to_name(st.last_error));
        }
    }
    printf("fault bitmap: 0x%08" PRIx32 "\n", bsp_get_fault_bitmap());
    return 0;
}

static int cmd_err(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("reset reason: %s\n", diag_errors_get_reset_reason_str());
    printf("crash count: %" PRIu32 "\n", diag_errors_get_crash_count());
    printf("fault bitmap: 0x%08" PRIx32 "\n", diag_errors_get_faults());
    return 0;
}

static int cmd_ota(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    ota_status_t st;
    (void)memset(&st, 0, sizeof(st));
    esp_err_t err = ota_service_get_status(&st);
    if (err != ESP_OK) {
        printf("ota status unavailable: %s\n", esp_err_to_name(err));
        return 0;
    }
    printf("state: %s\n", ota_service_get_state_name(st.state));
    printf("running: %s (%s)\n", st.running_version, st.running_label);
    printf("next slot: %s\n", st.next_label);
    printf("pending: %d (%s)\n", (int)st.pending, st.pending_version);
    printf("last result: %d (%s)\n", (int)st.last_result, st.last_result_detail);
    return 0;
}

static void s_mask_mac(const char *mac, char *out, size_t out_size)
{
    if ((mac == NULL) || (out == NULL) || (out_size < 12U)) {
        return;
    }
    /* 只显示前 3 个八位组，其余打码（3.8.4 脱敏硬性要求） */
    (void)snprintf(out, out_size, "%.8s**:**:**", mac);
}

static int cmd_wifi(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    prov_wifi_status_t st;
    (void)memset(&st, 0, sizeof(st));
    esp_err_t err = prov_get_wifi_status(&st);
    if (err != ESP_OK) {
        printf("wifi status unavailable: %s\n", esp_err_to_name(err));
        return 0;
    }
    printf("configured: %d\n", (int)st.configured);
    printf("ssid: %s\n", st.ssid);
    printf("rssi: %d dBm\n", (int)st.rssi);
    printf("ip: %s\n", st.ip);
    char mac[24];
    s_mask_mac(st.mac, mac, sizeof(mac));
    printf("mac: %s\n", mac);
    return 0;
}

static int cmd_nvs(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    prov_wifi_status_t wf;
    (void)memset(&wf, 0, sizeof(wf));
    (void)prov_get_wifi_status(&wf);
    printf("wifi configured: %d (ssid %s)\n", (int)wf.configured,
           wf.configured ? wf.ssid : "-");
    ota_status_t st;
    (void)memset(&st, 0, sizeof(st));
    (void)ota_service_get_status(&st);
    printf("ota pending: %d (%s)\n", (int)st.pending, st.pending_version);
    return 0;
}

static int cmd_log(int argc, char **argv)
{
    if (argc != 3) {
        printf("usage: log <tag|*> <verbose|debug|info|warn|error|none>\n");
        return 1;
    }
    esp_log_level_t level = ESP_LOG_INFO;
    if (strcmp(argv[2], "verbose") == 0) {
        level = ESP_LOG_VERBOSE;
    } else if (strcmp(argv[2], "debug") == 0) {
        level = ESP_LOG_DEBUG;
    } else if (strcmp(argv[2], "info") == 0) {
        level = ESP_LOG_INFO;
    } else if (strcmp(argv[2], "warn") == 0) {
        level = ESP_LOG_WARN;
    } else if (strcmp(argv[2], "error") == 0) {
        level = ESP_LOG_ERROR;
    } else if (strcmp(argv[2], "none") == 0) {
        level = ESP_LOG_NONE;
    } else {
        printf("unknown level %s\n", argv[2]);
        return 1;
    }
    esp_log_level_set(argv[1], level);
    printf("log level %s -> %d\n", argv[1], (int)level);
    return 0;
}

static int cmd_sd(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    bsp_sdcard_info_t info;
    if (bsp_sdcard_get_info(&info) == ESP_OK) {
        printf("sd total: %" PRIu64 " B, used: %" PRIu64 " B\n",
               info.total_bytes, info.used_bytes);
    } else {
        printf("sd not mounted\n");
    }
    DIR *d = opendir("/sdcard/logs");
    if (d != NULL) {
        struct dirent *e;
        printf("logs:\n");
        while ((e = readdir(d)) != NULL) {
            printf("  %s\n", e->d_name);
        }
        (void)closedir(d);
    }
    return 0;
}

static int cmd_snapshot(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    char buf[512];
    if (diag_snapshot_get_latest(buf, sizeof(buf)) == ESP_OK) {
        printf("%s\n", buf);
    }
    return 0;
}

esp_err_t diag_console_register(void)
{
    static const esp_console_cmd_t s_cmds[] = {
        { .command = "version",  .help = "firmware / IDF / build info", .hint = NULL, .func = cmd_version },
        { .command = "mem",      .help = "heap statistics",             .hint = NULL, .func = cmd_mem },
        { .command = "tasks",    .help = "task list + stack high-water",.hint = NULL, .func = cmd_tasks },
        { .command = "periph",   .help = "BSP module status",           .hint = NULL, .func = cmd_periph },
        { .command = "err",      .help = "faults / reset reason",       .hint = NULL, .func = cmd_err },
        { .command = "ota",      .help = "OTA status",                  .hint = NULL, .func = cmd_ota },
        { .command = "wifi",     .help = "WiFi status (masked)",        .hint = NULL, .func = cmd_wifi },
        { .command = "nvs",      .help = "key config overview",         .hint = NULL, .func = cmd_nvs },
        { .command = "log",      .help = "log <tag|*> <level>",         .hint = NULL, .func = cmd_log },
        { .command = "sd",       .help = "SD card + log files",         .hint = NULL, .func = cmd_sd },
        { .command = "snapshot", .help = "latest status snapshot",      .hint = NULL, .func = cmd_snapshot },
    };
    for (size_t i = 0U; i < (sizeof(s_cmds) / sizeof(s_cmds[0])); i++) {
        const esp_err_t err = esp_console_cmd_register(&s_cmds[i]);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "register %s failed: %s", s_cmds[i].command,
                     esp_err_to_name(err));
        }
    }
    return esp_console_register_help_command();
}

esp_err_t diag_console_start(void)
{
#if CONFIG_LERO_DIAG_CONSOLE
    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.max_cmdline_length = 256;
    repl_cfg.prompt = "lero>";
    esp_console_dev_uart_config_t uart_cfg = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    esp_console_repl_t *repl = NULL;
    esp_err_t err = esp_console_new_repl_uart(&uart_cfg, &repl_cfg, &repl);
    if (err == ESP_OK) {
        err = esp_console_start_repl(repl);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "console repl start failed: %s", esp_err_to_name(err));
    }
    return err;
#else
    return ESP_OK;
#endif
}

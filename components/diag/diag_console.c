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
#include <stdlib.h>
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
#include "bsp_imu.h"
#include "bsp_codec.h"
#include "bsp_touch.h"
#include "bsp_i2c.h"
#include "driver/i2c_master.h"
#include "provisioning.h"
#include "ota_service.h"
#include "player.h"
#include "voice.h"
#include "voice_internal.h"
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
    char log_dir[96];
    (void)snprintf(log_dir, sizeof(log_dir), "%s/logs", CONFIG_LERO_SD_BASE_PATH);
    DIR *d = opendir(log_dir);
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

static const char *s_player_state_name(player_state_t st)
{
    switch (st) {
    case PLAYER_STATE_IDLE:     return "idle";
    case PLAYER_STATE_PLAYING:  return "playing";
    case PLAYER_STATE_PAUSED:   return "paused";
    case PLAYER_STATE_FINISHED: return "finished";
    case PLAYER_STATE_ERROR:    return "error";
    default:                    return "?";
    }
}

static int cmd_play(int argc, char **argv)
{
    if (argc != 2) {
        printf("usage: play <path>  (e.g. play audio/test.mp3)\n");
        return 1;
    }
    esp_err_t err = player_play_file(argv[1]);
    printf("play: %s\n", (err == ESP_OK) ? "started" : esp_err_to_name(err));
    return 0;
}

static int cmd_play_loop(int argc, char **argv)
{
    if (argc != 2) {
        printf("usage: play-loop <path>  (SD file, repeat until stop)\n");
        return 1;
    }
    esp_err_t err = player_play_loop(argv[1]);
    printf("play-loop: %s\n", (err == ESP_OK) ? "started" : esp_err_to_name(err));
    return 0;
}

static int cmd_play_url(int argc, char **argv)
{
    if (argc != 2) {
        printf("usage: play-url <http(s) url>\n");
        printf("  stream the URL (no SD card needed) and play it in loop\n");
        printf("  (SD download variant: play-dl <url>)\n");
        return 1;
    }
    esp_err_t err = player_play_http(argv[1]);
    printf("play-url: %s\n", (err == ESP_OK) ? "streaming started" : esp_err_to_name(err));
    return 0;
}

static int cmd_play_dl(int argc, char **argv)
{
    if (argc != 2) {
        printf("usage: play-dl <http(s) url>\n");
        printf("  download the song to SD (/sdcard/download) then play it in loop\n");
        return 1;
    }
    esp_err_t err = player_play_url(argv[1]);
    printf("play-dl: %s\n", (err == ESP_OK) ? "download started" : esp_err_to_name(err));
    return 0;
}

static int cmd_rec(int argc, char **argv)
{
    uint32_t seconds = (uint32_t)CONFIG_LERO_VOICE_RECORD_DEFAULT_SECONDS;
    const uint32_t max_sec = (uint32_t)VOICE_RECORD_MEM_MAX_SECONDS;
    if (argc >= 2) {
        const int v = atoi(argv[1]);
        if ((v < 1) || ((uint32_t)v > max_sec)) {
            printf("usage: rec [seconds 1-%u]  (in-RAM, no SD needed)\n",
                   (unsigned)max_sec);
            return 1;
        }
        seconds = (uint32_t)v;
    }
    /* NULL path → 内存录音（PSRAM），录完自动回放 */
    esp_err_t err = voice_record_start(seconds, NULL);
    printf("rec %u s: %s\n", (unsigned)seconds,
           (err == ESP_OK) ? "recording" : esp_err_to_name(err));
    return 0;
}

static int cmd_play_mem(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    const uint8_t *buf = NULL;
    size_t sz = 0U;
    const esp_err_t get = voice_capture_get_rec_mem(&buf, &sz);
    if (get != ESP_OK) {
        printf("play-mem: no recording yet (run 'rec' first)\n");
        return 1;
    }
    const esp_err_t err = player_play_mem(buf, sz);
    printf("play-mem: %s (%u B from RAM)\n",
           (err == ESP_OK) ? "playing" : esp_err_to_name(err), (unsigned)sz);
    return 0;
}

static int cmd_rec_stop(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    (void)voice_record_stop();
    printf("rec-stop\n");
    return 0;
}

static int cmd_imu(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    if (!bsp_imu_is_present()) {
        printf("imu: not found\n");
        return 0;
    }
    bsp_imu_vec3_t a;
    bsp_imu_vec3_t g;
    int32_t t = 0;
    if (bsp_imu_read_accel(&a) == ESP_OK) {
        printf("accel: x=%ld y=%ld z=%ld (mg)\n",
               (long)a.x, (long)a.y, (long)a.z);
    }
    if (bsp_imu_read_gyro(&g) == ESP_OK) {
        printf("gyro:  x=%ld y=%ld z=%ld (mdps)\n",
               (long)g.x, (long)g.y, (long)g.z);
    }
    if (bsp_imu_read_temp(&t) == ESP_OK) {
        printf("temp:  %ld.%02ld C\n", (long)(t / 1000), (long)((t % 1000) / 10));
    }
    return 0;
}

static void s_scan_bus(const char *name, i2c_master_bus_handle_t bus)
{
    printf("%s: ", name);
    bool any = false;
    for (uint16_t addr = 0x03U; addr <= 0x77U; addr++) {
        if (i2c_master_probe(bus, addr, 20) == ESP_OK) {
            printf("0x%02X ", (unsigned)addr);
            any = true;
        }
    }
    printf("%s\n", any ? "" : "(none)");
}

static int cmd_i2c_scan(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    i2c_master_bus_handle_t bus = NULL;
    if (bsp_i2c_get_bus0(&bus) == ESP_OK) {
        s_scan_bus("I2C0 (codec/imu)", bus);
    } else {
        printf("I2C0: bus not created\n");
    }
    if (bsp_i2c_get_bus1(&bus) == ESP_OK) {
        s_scan_bus("I2C1 (touch)", bus);
    } else {
        printf("I2C1: bus not created\n");
    }
    return 0;
}

static int cmd_reg(int argc, char **argv)
{
    if (argc != 4) {
        printf("usage: reg <bus0|bus1> <addr7> <reg>\n");
        return 1;
    }
    i2c_master_bus_handle_t bus = NULL;
    esp_err_t err = (strcmp(argv[1], "bus1") == 0) ? bsp_i2c_get_bus1(&bus)
                                                   : bsp_i2c_get_bus0(&bus);
    if (err != ESP_OK) {
        printf("bus unavailable\n");
        return 0;
    }
    const uint16_t addr = (uint16_t)strtol(argv[2], NULL, 0);
    const uint8_t reg = (uint8_t)strtol(argv[3], NULL, 0);
    i2c_master_dev_handle_t dev = NULL;
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 400000U,
    };
    err = i2c_master_bus_add_device(bus, &dev_cfg, &dev);
    if (err != ESP_OK) {
        printf("add device 0x%02X failed\n", (unsigned)addr);
        return 0;
    }
    uint8_t val = 0U;
    err = i2c_master_transmit_receive(dev, &reg, 1U, &val, 1U, 100);
    if (err == ESP_OK) {
        printf("0x%02X[0x%02X] = 0x%02X\n", (unsigned)addr, (unsigned)reg, (unsigned)val);
    } else {
        printf("read failed: %s\n", esp_err_to_name(err));
    }
    (void)i2c_master_bus_rm_device(dev);
    return 0;
}

static int cmd_codec(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    if (!bsp_codec_is_present()) {
        printf("codec: not found (AUD_3V3 / 0x20 / I2C0)\n");
        return 0;
    }
    printf("ES8389 registers:\n");
    static const uint8_t groups[][2] = {
        { 0x00U, 0x0AU },  /* 复位/时钟管理 */
        { 0x21U, 0x26U },  /* ADC 控制 */
        { 0x40U, 0x43U },  /* DAC 控制 */
        { 0x60U, 0x64U },  /* 模拟 */
    };
    for (size_t g = 0U; g < (sizeof(groups) / sizeof(groups[0])); g++) {
        for (uint8_t r = groups[g][0]; r <= groups[g][1]; r++) {
            uint8_t v = 0U;
            if (bsp_codec_read_reg(r, &v) == ESP_OK) {
                printf("  0x%02X = 0x%02X\n", (unsigned)r, (unsigned)v);
            } else {
                printf("  0x%02X = ERR\n", (unsigned)r);
            }
        }
    }
    return 0;
}

static int cmd_power(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    uint32_t mv = 0U;
    if (bsp_power_get_battery_mv(&mv) == ESP_OK) {
        printf("battery: %lu mV\n", (unsigned long)mv);
    } else {
        printf("battery: n/a\n");
    }
    if (bsp_power_get_bus_mv(&mv) == ESP_OK) {
        printf("bus:     %lu mV\n", (unsigned long)mv);
    } else {
        printf("bus: n/a\n");
    }
    bool charging = false;
    if (bsp_power_get_charge_state(&charging) == ESP_OK) {
        printf("charging: %d\n", (int)charging);
    }
    uint8_t pct = 0U;
    if (bsp_power_get_battery_pct(&pct) == ESP_OK) {
        printf("battery pct: %u%%\n", (unsigned)pct);
    }
    return 0;
}

static int cmd_touch(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    if (bsp_touch_get_handle() == NULL) {
        printf("touch: not initialized\n");
        return 0;
    }
    bsp_touch_point_t p;
    esp_err_t err = bsp_touch_read_point(&p);
    if (err == ESP_OK) {
        printf("touch: %s", p.pressed ? "pressed" : "idle");
        if (p.pressed) {
            printf(" x=%u y=%u", (unsigned)p.x, (unsigned)p.y);
        }
        printf("\n");
    } else {
        printf("touch read failed: %s\n", esp_err_to_name(err));
    }
    return 0;
}

static int cmd_stop(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    (void)player_stop();
    printf("stop\n");
    return 0;
}

static int cmd_pause(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    (void)player_pause();
    printf("pause\n");
    return 0;
}

static int cmd_resume(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    (void)player_resume();
    printf("resume\n");
    return 0;
}

static int cmd_vol(int argc, char **argv)
{
    if (argc != 2) {
        printf("usage: vol <0-100>\n");
        return 1;
    }
    const int v = atoi(argv[1]);
    if ((v < 0) || (v > 100)) {
        printf("volume out of range\n");
        return 1;
    }
    esp_err_t err = player_set_volume((uint8_t)v);
    printf("volume %d: %s\n", v, (err == ESP_OK) ? "ok" : esp_err_to_name(err));
    return 0;
}

static int cmd_player(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    player_state_t st = PLAYER_STATE_IDLE;
    (void)player_get_state(&st);
    printf("player state: %s\n", s_player_state_name(st));
    return 0;
}

static int cmd_ota_check(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    esp_err_t err = ota_service_check_http();
    printf("ota check: %s\n", (err == ESP_OK) ? "started" : esp_err_to_name(err));
    return 0;
}

static int cmd_ota_sd(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    esp_err_t err = ota_service_apply_sd();
    printf("ota sd: %s\n", (err == ESP_OK) ? "started" : esp_err_to_name(err));
    return 0;
}

static int cmd_ota_confirm(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    esp_err_t err = ota_service_confirm();
    printf("ota confirm: %s\n", (err == ESP_OK) ? "ok, rebooting" : esp_err_to_name(err));
    return 0;
}

static int cmd_ota_cancel(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    esp_err_t err = ota_service_cancel();
    printf("ota cancel: %s\n", (err == ESP_OK) ? "ok" : esp_err_to_name(err));
    return 0;
}

static const char *s_voice_state_name(voice_state_t st)
{
    switch (st) {
    case VOICE_STATE_IDLE:       return "idle";
    case VOICE_STATE_LISTENING:  return "listening";
    case VOICE_STATE_PROCESSING: return "processing";
    case VOICE_STATE_SPEAKING:   return "speaking";
    default:                     return "?";
    }
}

static int cmd_voice(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    voice_state_t st = VOICE_STATE_IDLE;
    (void)voice_get_state(&st);
    printf("voice state: %s\n", s_voice_state_name(st));
    return 0;
}

static int cmd_voice_listen(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    esp_err_t err = voice_listen_start();
    printf("voice listen: %s\n", (err == ESP_OK) ? "started" : esp_err_to_name(err));
    return 0;
}

static int cmd_voice_stop(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    (void)voice_listen_stop();
    printf("voice stop\n");
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
        { .command = "ota-check",.help = "check + download HTTP OTA",   .hint = NULL, .func = cmd_ota_check },
        { .command = "ota-sd",   .help = "force SD card OTA (downgrade ok)", .hint = NULL, .func = cmd_ota_sd },
        { .command = "ota-confirm", .help = "confirm pending update + reboot", .hint = NULL, .func = cmd_ota_confirm },
        { .command = "ota-cancel",  .help = "cancel pending update",    .hint = NULL, .func = cmd_ota_cancel },
        { .command = "wifi",     .help = "WiFi status (masked)",        .hint = NULL, .func = cmd_wifi },
        { .command = "nvs",      .help = "key config overview",         .hint = NULL, .func = cmd_nvs },
        { .command = "log",      .help = "log <tag|*> <level>",         .hint = NULL, .func = cmd_log },
        { .command = "sd",       .help = "SD card + log files",         .hint = NULL, .func = cmd_sd },
        { .command = "snapshot", .help = "latest status snapshot",      .hint = NULL, .func = cmd_snapshot },
        { .command = "play",     .help = "play <path> (SD audio file)", .hint = NULL, .func = cmd_play },
        { .command = "play-loop",.help = "play <path> in loop until stop", .hint = NULL, .func = cmd_play_loop },
        { .command = "play-url", .help = "stream http(s) URL + loop play (no SD)", .hint = NULL, .func = cmd_play_url },
        { .command = "play-dl",  .help = "download http(s) URL to SD + loop play", .hint = NULL, .func = cmd_play_dl },
        { .command = "rec",      .help = "record N s to RAM WAV + auto play (no SD)", .hint = NULL, .func = cmd_rec },
        { .command = "play-mem", .help = "play last in-RAM recording (no SD)", .hint = NULL, .func = cmd_play_mem },
        { .command = "rec-stop", .help = "stop recording early",        .hint = NULL, .func = cmd_rec_stop },
        { .command = "stop",     .help = "stop playback",               .hint = NULL, .func = cmd_stop },
        { .command = "pause",    .help = "pause playback",              .hint = NULL, .func = cmd_pause },
        { .command = "resume",   .help = "resume playback",             .hint = NULL, .func = cmd_resume },
        { .command = "vol",      .help = "vol <0-100>",                 .hint = NULL, .func = cmd_vol },
        { .command = "player",   .help = "player state",                .hint = NULL, .func = cmd_player },
        { .command = "voice",    .help = "voice state",                 .hint = NULL, .func = cmd_voice },
        { .command = "voice-listen", .help = "start voice listen",      .hint = NULL, .func = cmd_voice_listen },
        { .command = "voice-stop",   .help = "stop voice listen",       .hint = NULL, .func = cmd_voice_stop },
        { .command = "imu",      .help = "read QMI8658A accel/gyro/temp", .hint = NULL, .func = cmd_imu },
        { .command = "i2c-scan", .help = "scan I2C0/I2C1 for devices",    .hint = NULL, .func = cmd_i2c_scan },
        { .command = "reg",      .help = "reg <bus0|bus1> <addr7> <reg>", .hint = NULL, .func = cmd_reg },
        { .command = "codec",    .help = "ES8389 register dump",          .hint = NULL, .func = cmd_codec },
        { .command = "power",    .help = "battery/bus voltage + charging",.hint = NULL, .func = cmd_power },
        { .command = "touch",    .help = "FT6336U status + coordinates",  .hint = NULL, .func = cmd_touch },
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

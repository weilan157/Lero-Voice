/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file player.c
 * @brief SD card audio player implementation (docs/PLAN.md 6.2).
 *
 * Pipeline: file://sdcard/... -> esp_audio_simple_player (ESP-GMF decoders +
 * transformers) -> PCM out callback -> esp_codec_dev (ES8389 via I2S).
 *
 * The codec is opened on ESP_ASP_EVENT_TYPE_MUSIC_INFO (sample rate /
 * channels / bits from the decoded stream) and closed on STOPPED / FINISHED /
 * ERROR. The PA (NS4150B) is enabled only while a stream is active
 * (docs/PLAN.md 3.3.1 #3, no pop at boot).
 *
 * Loop mode (player_play_loop / player_play_url): on FINISHED the same URI
 * is re-run automatically until player_stop(). The codec stays open across
 * restarts (sample-rate change still re-opens through MUSIC_INFO).
 *
 * URL download (player_play_url): a static download task streams the file
 * via esp_http_client into /sdcard/download/<name>, then starts loop
 * playback. No dynamic allocation in this component (HTTP client internals
 * are part of the IDF library, out of our MISRA scope).
 */

#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_audio_simple_player.h"
#include "esp_codec_dev.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bsp_codec.h"
#include "bsp_amplifier.h"
#include "bsp_sdcard.h"
#include "player.h"
#include "player_memfs.h"

#define TAG "player"

#define PLAYER_URI_MAX      384U   /* file URI 与 http(s) URL 共用上限 */
/* 注意三斜杠：file IO 的 get_mount_path 仅当 "://" 后是 '/' 时返回绝对路径 */
#define PLAYER_URI_PREFIX   "file:///sdcard/"

static esp_asp_handle_t s_player;
static esp_codec_dev_handle_t s_codec;
static bool s_codec_open;
static player_state_t s_state;
static player_event_cb_t s_cb;

/* ---------------- 循环播放状态 ---------------- */
static bool s_loop;
static char s_loop_uri[PLAYER_URI_MAX];

/* ---------------- 播放控制同步（切歌/重播竞态防护） ----------------
 * 问题：esp_audio_simple_player_run 内部检查 state（pipeline 任务异步
 * 更新），stop 后立即 run 有 INVALID_STATE 窗口；迟到的 STOPPED 事件
 * 会误关新流 codec；事件回调内 run 存在自等待重入。
 * 方案：调用者上下文 stop 后等 STOPPED 确认（s_stop_acked）再 run；
 * 事件回调只置标志，重播由独立控制任务执行（非 pipeline 任务）。 */
static TaskHandle_t s_ctrl_task;
static StaticTask_t s_ctrl_tcb;
static StackType_t s_ctrl_stack[CONFIG_LERO_PLAYER_CTRL_TASK_STACK / sizeof(StackType_t)];
static volatile bool s_stop_requested;  /* 切歌中：下一次 STOPPED 视为 ack */
static volatile bool s_stop_acked;      /* STOPPED 已落地 */
static volatile bool s_loop_pending;    /* 事件回调请求重播 */
static uint32_t s_gen;                  /* 播放代次（每次 run 递增） */
static uint32_t s_active_gen;           /* 当前活跃流代次 */
static volatile uint32_t s_last_run_tick;   /* 最近一次 run 的 tick（陈旧 STOPPED 抑制） */
#define PLAYER_STALE_STOP_MS  250U      /* run 后此窗口内的 STOPPED 视为陈旧 */

/* ---------------- URL 下载任务 ---------------- */
#define PLAYER_DL_NAME_MAX   64U     /* 文件名上限（FAT 长文件名支持，且
                                      * 保证 /sdcard/download/<name> 总长
                                      * 不触发 -Werror=format-truncation） */
static FILE *s_dl_file;
static char s_dl_url[CONFIG_LERO_PLAYER_DL_URL_MAX];
static volatile bool s_dl_busy;
static TaskHandle_t s_dl_task;
static StaticTask_t s_dl_tcb;
static StackType_t s_dl_stack[CONFIG_LERO_PLAYER_DL_TASK_STACK / sizeof(StackType_t)];

/* ------------------------------------------------------------------------- */
/* PCM / event callbacks (run in the ESP-GMF player task context)            */
/* ------------------------------------------------------------------------- */

static int s_pcm_out(uint8_t *data, int data_size, void *ctx)
{
    (void)ctx;
    if ((s_codec == NULL) || !s_codec_open || (data_size <= 0)) {
        return 0;                   /* 未就绪时丢弃，避免写未打开设备 */
    }
    (void)esp_codec_dev_write(s_codec, data, (size_t)data_size);
    return 0;
}

static void s_finish(player_state_t state)
{
    s_loop = false;
    s_loop_uri[0] = '\0';
    if (s_codec_open) {
        (void)esp_codec_dev_close(s_codec);
        s_codec_open = false;
    }
    /* 播放结束：先静音再关功放电源（防爆音），并复位状态机 */
    (void)bsp_amp_mute(true);
    (void)bsp_amp_enable(false);
    s_state = state;
    if (s_cb != NULL) {
        s_cb(state, NULL);
    }
}

static void s_finish(player_state_t state);
static esp_err_t s_play_uri(const char *uri, bool loop);   /* 定义在下方，s_event 先引用 */
static void s_ctrl_task_fn(void *arg);                     /* 播放控制任务（循环重播） */

static int s_event(esp_asp_event_pkt_t *event, void *ctx)
{
    (void)ctx;
    if (event == NULL) {
        return 0;
    }
    if (event->type == ESP_ASP_EVENT_TYPE_MUSIC_INFO) {
        esp_asp_music_info_t info;
        (void)memset(&info, 0, sizeof(info));
        if (event->payload_size > (int)sizeof(info)) {
            event->payload_size = (int)sizeof(info);
        }
        (void)memcpy(&info, event->payload, (size_t)event->payload_size);
        ESP_LOGI(TAG, "music info: %d Hz, %d ch, %d bit",
                 (int)info.sample_rate, (int)info.channels, (int)info.bits);
        if ((s_codec != NULL) && !s_codec_open) {
            esp_codec_dev_sample_info_t fs = {
                .sample_rate = info.sample_rate,
                .channel = info.channels,
                .bits_per_sample = info.bits,
            };
            if (esp_codec_dev_open(s_codec, &fs) == ESP_CODEC_DEV_OK) {
                s_codec_open = true;
                /* 功放：使能 + 解除静音（bsp_amp_init 默认 muted 防爆音，
                 * 两个条件都满足 PA_CTRL(IO52) 才输出高电平） */
                (void)bsp_amp_enable(true);
                (void)bsp_amp_mute(false);
            } else {
                ESP_LOGE(TAG, "codec open failed");
            }
        }
    } else if (event->type == ESP_ASP_EVENT_TYPE_STATE) {
        esp_asp_state_t st = (esp_asp_state_t)0;   /* 枚举首值：无数据时安全 */
        if (event->payload_size >= (int)sizeof(st)) {
            (void)memcpy(&st, event->payload, sizeof(st));
        }
        switch (st) {
        case ESP_ASP_STATE_RUNNING:
            s_state = PLAYER_STATE_PLAYING;
            if (s_cb != NULL) {
                s_cb(s_state, NULL);
            }
            break;
        case ESP_ASP_STATE_PAUSED:
            s_state = PLAYER_STATE_PAUSED;
            if (s_cb != NULL) {
                s_cb(s_state, NULL);
            }
            break;
        case ESP_ASP_STATE_STOPPED:
            /* 切歌：本事件作为 stop 的 ack（不 s_finish，codec 留给新流）。
             * 手动 stop：正常 s_finish（关 codec）。
             * 陈旧 STOPPED（ack 超时后 run 已发出）：run 后短窗口内忽略。 */
            s_stop_acked = true;
            if (s_stop_requested) {
                s_stop_requested = false;
            } else if ((xTaskGetTickCount() - s_last_run_tick) >
                       pdMS_TO_TICKS(PLAYER_STALE_STOP_MS)) {
                s_finish(PLAYER_STATE_FINISHED);
            }
            break;
        case ESP_ASP_STATE_FINISHED:
            if (s_loop && (s_loop_uri[0] != '\0')) {
                /* 循环模式：事件回调只置标志，由控制任务执行完整重开
                 * （避免在 pipeline 任务上下文重入 run；完整重开防
                 * I2S 时钟间隙失锁噪声） */
                s_loop_pending = true;
                if (s_ctrl_task != NULL) {
                    (void)xTaskNotifyGive(s_ctrl_task);
                }
            } else {
                s_finish(PLAYER_STATE_FINISHED);
            }
            break;
        case ESP_ASP_STATE_ERROR:
            ESP_LOGE(TAG, "player error");
            s_finish(PLAYER_STATE_ERROR);
            break;
        default:
            break;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Public API                                                                */
/* ------------------------------------------------------------------------- */

esp_err_t player_init(void)
{
    if (s_player != NULL) {
        return ESP_OK;
    }
    s_codec = bsp_codec_get_handle();
    if (s_codec == NULL) {
        ESP_LOGE(TAG, "codec not ready (bsp_codec init failed?)");
        return ESP_ERR_NOT_FOUND;
    }
    esp_asp_cfg_t cfg = {
        .out.cb = s_pcm_out,
        .out.user_ctx = NULL,
        .task_prio = CONFIG_LERO_PLAYER_TASK_PRIORITY,
        .task_stack = CONFIG_LERO_PLAYER_TASK_STACK,
    };
    esp_gmf_err_t err = esp_audio_simple_player_new(&cfg, &s_player);
    if (err != ESP_GMF_ERR_OK) {
        ESP_LOGE(TAG, "simple player new failed: %d", (int)err);
        return ESP_FAIL;
    }
    err = esp_audio_simple_player_set_event(s_player, s_event, NULL);
    if (err != ESP_GMF_ERR_OK) {
        ESP_LOGE(TAG, "set event failed: %d", (int)err);
        (void)esp_audio_simple_player_destroy(s_player);
        s_player = NULL;
        return ESP_FAIL;
    }
    s_state = PLAYER_STATE_IDLE;
    (void)player_set_volume((uint8_t)CONFIG_LERO_PLAYER_DEFAULT_VOLUME);
    /* 内存 WAV 播放 VFS（/mem，无 SD 时 rec 回放用） */
    (void)player_memfs_init();
    /* 播放控制任务（循环重播，避免在 pipeline 任务上下文重入 run） */
    s_ctrl_task = xTaskCreateStaticPinnedToCore(
        s_ctrl_task_fn, "player_ctrl", (uint32_t)(sizeof(s_ctrl_stack) / sizeof(StackType_t)),
        NULL, CONFIG_LERO_PLAYER_CTRL_TASK_PRIORITY, s_ctrl_stack, &s_ctrl_tcb,
        CONFIG_LERO_PLAYER_CTRL_TASK_CORE);
    ESP_LOGI(TAG, "player ready (esp_audio_simple_player)");
    return ESP_OK;
}

static esp_err_t s_build_uri(const char *path, char *uri, size_t uri_size)
{
    if ((path == NULL) || (uri == NULL) || (uri_size == 0U)) {
        return ESP_ERR_INVALID_ARG;
    }
    const char *rel = path;
    if (rel[0] == '/') {
        rel++;                          /* 去掉绝对路径前导 '/' */
    }
    if (strncmp(rel, "sdcard/", 7U) == 0) {
        rel += 7U;                      /* 兼容 "sdcard/..." 写法 */
    }
    if (rel[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if ((strlen(PLAYER_URI_PREFIX) + strlen(rel) + 1U) > uri_size) {
        return ESP_ERR_INVALID_SIZE;
    }
    (void)snprintf(uri, uri_size, "%s%s", PLAYER_URI_PREFIX, rel);
    return ESP_OK;
}

/* 内部播放入口：uri 为完整 URI；loop 指定是否循环。
 * 注意：只在"调用者上下文"（console/voice/BT 焦点）调用，
 * 事件回调（pipeline 任务）不得调用——重播走 s_ctrl_task。 */
static esp_err_t s_run_uri(const char *uri, bool loop)
{
    if ((s_player == NULL) || (uri == NULL)) {
        return ESP_ERR_INVALID_STATE;
    }
    s_loop = loop;
    if (loop) {
        (void)strlcpy(s_loop_uri, uri, sizeof(s_loop_uri));
    } else {
        s_loop_uri[0] = '\0';
    }
    s_gen++;
    s_active_gen = s_gen;
    s_last_run_tick = (uint32_t)xTaskGetTickCount();
    esp_gmf_err_t gerr = esp_audio_simple_player_run(s_player, uri, NULL);
    if (gerr != ESP_GMF_ERR_OK) {
        ESP_LOGE(TAG, "run %s failed: %d", uri, (int)gerr);
        s_loop = false;
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "playing %s%s", uri, loop ? " (loop)" : "");
    return ESP_OK;
}

/* 停止当前播放并等待 STOPPED 事件落地（避免迟到事件误伤新流） */
static void s_stop_current(void)
{
    if (s_state != PLAYER_STATE_PLAYING && s_state != PLAYER_STATE_PAUSED) {
        return;
    }
    s_stop_requested = true;
    s_stop_acked = false;
    (void)esp_audio_simple_player_stop(s_player);
    for (int i = 0; (i < 50) && !s_stop_acked; i++) {
        vTaskDelay(pdMS_TO_TICKS(10U));
    }
    s_stop_requested = false;
    s_stop_acked = false;
}

/* 控制任务：消费事件回调的请求（循环重播），避免 pipeline 任务重入 */
static void s_ctrl_task_fn(void *arg)
{
    (void)arg;
    for (;;) {
        uint32_t notify = 0U;
        (void)xTaskNotifyWait(0U, UINT32_MAX, &notify, pdMS_TO_TICKS(500U));
        if (!s_loop_pending) {
            continue;
        }
        s_loop_pending = false;
        if ((s_loop_uri[0] == '\0') || (s_player == NULL)) {
            continue;
        }
        char uri[PLAYER_URI_MAX];
        (void)strlcpy(uri, s_loop_uri, sizeof(uri));
        ESP_LOGI(TAG, "loop: replaying %s", uri);
        /* 完整重开：先关 codec/功放（I2S 时钟间隙防失锁），再重新 run */
        s_finish(PLAYER_STATE_IDLE);
        if (s_run_uri(uri, true) != ESP_OK) {
            ESP_LOGE(TAG, "loop replay failed");
            s_finish(PLAYER_STATE_ERROR);
        }
    }
}

/* 公开入口：正在播放则先同步停止，再开新流（消除切歌竞态） */
static esp_err_t s_play_uri(const char *uri, bool loop)
{
    if ((s_player == NULL) || (uri == NULL)) {
        return ESP_ERR_INVALID_STATE;
    }
    s_stop_current();
    return s_run_uri(uri, loop);
}

esp_err_t player_play_file(const char *path)
{
    esp_err_t err = bsp_sdcard_poll();          /* 挂载 SD（延迟挂载 + 轮询） */
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SD unavailable: %s", esp_err_to_name(err));
        s_state = PLAYER_STATE_ERROR;
        return err;
    }
    char uri[PLAYER_URI_MAX];
    err = s_build_uri(path, uri, sizeof(uri));
    if (err != ESP_OK) {
        return err;
    }
    return s_play_uri(uri, false);
}

esp_err_t player_play_loop(const char *path)
{
    esp_err_t err = bsp_sdcard_poll();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SD unavailable: %s", esp_err_to_name(err));
        s_state = PLAYER_STATE_ERROR;
        return err;
    }
    char uri[PLAYER_URI_MAX];
    err = s_build_uri(path, uri, sizeof(uri));
    if (err != ESP_OK) {
        return err;
    }
    return s_play_uri(uri, true);
}

esp_err_t player_play_http(const char *url)
{
    if ((url == NULL) || (s_player == NULL)) {
        return ESP_ERR_INVALID_STATE;
    }
    /* 仅接受 http(s) scheme；直接流式播放（不落盘，无需 SD 卡）。
     * esp_audio_simple_player 按 URI scheme 选择 IO：http:// → HTTP stream */
    if ((strncasecmp(url, "http://", 7U) != 0) && (strncasecmp(url, "https://", 8U) != 0)) {
        ESP_LOGE(TAG, "unsupported scheme (need http:// or https://): %.32s", url);
        return ESP_ERR_INVALID_ARG;
    }
    if (strlen(url) >= sizeof(s_loop_uri)) {
        ESP_LOGE(TAG, "url too long (%u >= %u)", (unsigned)strlen(url),
                 (unsigned)sizeof(s_loop_uri));
        return ESP_ERR_INVALID_SIZE;
    }
    return s_play_uri(url, true);   /* 循环：FINISHED 后重新请求同一 URL */
}

esp_err_t player_play_mem(const uint8_t *data, size_t size)
{
    if ((data == NULL) || (size < 44U) || (s_player == NULL)) {
        return ESP_ERR_INVALID_STATE;
    }
    /* 先停当前播放（等 STOPPED 落地）再换 memfs 数据指针：
     * 否则 pipeline 任务正读旧数据时换底导致播放错乱 */
    s_stop_current();
    esp_err_t err = player_memfs_set_data(data, size);
    if (err != ESP_OK) {
        return err;
    }
    /* 走 file IO + /mem VFS：不碰 SD，纯内存播放（WAV 由扩展名选择解码器）。
     * 三斜杠：get_mount_path 才能解析出绝对路径 /mem/rec.wav */
    return s_run_uri("file:///mem/rec.wav", false);
}

esp_err_t player_stop(void)
{
    if (s_player == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_gmf_err_t err = esp_audio_simple_player_stop(s_player);
    return (err == ESP_GMF_ERR_OK) ? ESP_OK : ESP_FAIL;
}

esp_err_t player_pause(void)
{
    if (s_player == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_gmf_err_t err = esp_audio_simple_player_pause(s_player);
    return (err == ESP_GMF_ERR_OK) ? ESP_OK : ESP_FAIL;
}

esp_err_t player_resume(void)
{
    if (s_player == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_gmf_err_t err = esp_audio_simple_player_resume(s_player);
    return (err == ESP_GMF_ERR_OK) ? ESP_OK : ESP_FAIL;
}

esp_err_t player_set_volume(uint8_t pct)
{
    if (pct > 100U) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_codec == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    if (esp_codec_dev_set_out_vol(s_codec, (int)pct) != ESP_CODEC_DEV_OK) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t player_get_state(player_state_t *state)
{
    if (state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *state = s_state;
    return ESP_OK;
}

void player_register_event_cb(player_event_cb_t cb)
{
    s_cb = cb;
}

/* ------------------------------------------------------------------------- */
/* URL 下载 + 循环播放（player_play_url）                                      */
/* ------------------------------------------------------------------------- */

/* 从 URL 提取文件名：取最后一个 '/' 之后、'?' 之前；无扩展名时补 .mp3 */
static void s_extract_filename(const char *url, char *out, size_t out_size)
{
    out[0] = '\0';
    if ((url == NULL) || (out_size == 0U)) {
        return;
    }
    const char *slash = strrchr(url, '/');
    const char *base = (slash != NULL) ? (slash + 1) : url;
    const char *q = strchr(base, '?');
    const size_t len = (q != NULL) ? (size_t)(q - base) : strlen(base);
    if ((len == 0U) || (len >= out_size)) {
        (void)strlcpy(out, "song.mp3", out_size);
        return;
    }
    (void)memcpy(out, base, len);
    out[len] = '\0';
    /* 无扩展名：补 .mp3（多数音乐直链带扩展名，此处兜底） */
    if (strrchr(out, '.') == NULL) {
        (void)strlcat(out, ".mp3", out_size);
    }
}

static esp_err_t s_http_event(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        if (s_dl_file != NULL) {
            (void)fwrite(evt->data, 1U, (size_t)evt->data_len, s_dl_file);
        }
    }
    return ESP_OK;
}

static void s_download_task(void *arg)
{
    (void)arg;
    for (;;) {
        uint32_t notify = 0U;
        (void)xTaskNotifyWait(0U, UINT32_MAX, &notify, portMAX_DELAY);
        if ((notify & 1U) == 0U) {
            continue;
        }

        s_dl_file = NULL;
        esp_err_t err = bsp_sdcard_poll();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "download: SD unavailable");
            s_dl_busy = false;
            continue;
        }

        char name[PLAYER_DL_NAME_MAX];
        s_extract_filename(s_dl_url, name, sizeof(name));
        char dir[96];
        (void)snprintf(dir, sizeof(dir), "%s", CONFIG_LERO_PLAYER_DL_DIR);
        (void)mkdir(dir, 0755);

        char path[128];
        (void)snprintf(path, sizeof(path), "%s/%s", CONFIG_LERO_PLAYER_DL_DIR, name);
        ESP_LOGI(TAG, "download: %s -> %s", s_dl_url, path);

        s_dl_file = fopen(path, "wb");
        if (s_dl_file == NULL) {
            ESP_LOGE(TAG, "download: open %s failed", path);
            s_dl_busy = false;
            continue;
        }

        esp_http_client_config_t cfg = {
            .url = s_dl_url,
            .event_handler = s_http_event,
            .timeout_ms = CONFIG_LERO_PLAYER_DL_TIMEOUT_MS,
            .buffer_size = CONFIG_LERO_PLAYER_DL_BUFFER,
        };
        esp_http_client_handle_t client = esp_http_client_init(&cfg);
        if (client == NULL) {
            ESP_LOGE(TAG, "download: http client init failed");
            (void)fclose(s_dl_file);
            s_dl_file = NULL;
            s_dl_busy = false;
            continue;
        }
        err = esp_http_client_perform(client);
        esp_http_client_cleanup(client);
        (void)fclose(s_dl_file);
        s_dl_file = NULL;

        if (err != ESP_OK) {
            ESP_LOGE(TAG, "download failed: %s", esp_err_to_name(err));
            (void)remove(path);
            s_dl_busy = false;
            continue;
        }
        s_dl_busy = false;
        ESP_LOGI(TAG, "download done, playing loop: %s", path);
        (void)player_play_loop(path);       /* 下载完成：循环播放 */
    }
}

esp_err_t player_play_url(const char *url)
{
    if ((url == NULL) || (s_player == NULL)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_dl_busy) {
        ESP_LOGW(TAG, "download already in progress");
        return ESP_ERR_INVALID_STATE;
    }
    if (strlen(url) >= sizeof(s_dl_url)) {
        return ESP_ERR_INVALID_SIZE;
    }
    (void)strlcpy(s_dl_url, url, sizeof(s_dl_url));

    if (s_dl_task == NULL) {
        s_dl_task = xTaskCreateStaticPinnedToCore(
            s_download_task, "player_dl",
            (uint32_t)(sizeof(s_dl_stack) / sizeof(StackType_t)),
            NULL, CONFIG_LERO_PLAYER_DL_TASK_PRIORITY,
            s_dl_stack, &s_dl_tcb, CONFIG_LERO_PLAYER_DL_TASK_CORE);
        if (s_dl_task == NULL) {
            return ESP_FAIL;
        }
    }
    s_dl_busy = true;
    (void)xTaskNotifyGive(s_dl_task);
    ESP_LOGI(TAG, "download scheduled: %s", url);
    return ESP_OK;
}

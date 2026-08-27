/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file voice_capture.c
 * @brief Mic capture via esp_codec_dev (ES8389 ADC, dual mic) + simple VAD.
 *
 * The codec is shared duplex with the player (esp_codec_dev IN_OUT handle);
 * the I2S clock domain forces one sample rate, so capture uses the same
 * 48 kHz / 2 ch / 16 bit format as playback (docs/PLAN.md 3.5.3 rule 2).
 * Cloud ASR either accepts 48 kHz or resamples server-side (M9 detail).
 *
 * VAD v1: RMS threshold + tail timeout; enough for skeleton/telemetry.
 * Replace with ESP-SR AFE (AEC/NS) when the S31 support matrix allows.
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_codec_dev.h"
#include "bsp_codec.h"
#include "bsp_sdcard.h"
#include "voice_internal.h"

#define TAG "voice_capture"

#define FRAME_SAMPLES \
    ((CONFIG_LERO_VOICE_SAMPLE_RATE * CONFIG_LERO_VOICE_CHANNELS * CONFIG_LERO_VOICE_FRAME_MS) / 1000)
#define FRAME_BYTES    (FRAME_SAMPLES * 2U)

static esp_codec_dev_handle_t s_codec;
static int16_t s_pcm[FRAME_SAMPLES];

esp_err_t voice_capture_init(void)
{
    s_codec = bsp_codec_get_handle();
    if (s_codec == NULL) {
        ESP_LOGE(TAG, "codec not ready (bsp_codec init failed?)");
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "capture ready (%d Hz / %d ch / 16 bit, frame %d ms)",
             CONFIG_LERO_VOICE_SAMPLE_RATE, CONFIG_LERO_VOICE_CHANNELS,
             CONFIG_LERO_VOICE_FRAME_MS);
    return ESP_OK;
}

/* 简化 VAD：帧 RMS 超阈值即视为语音 */
static bool s_frame_has_speech(const int16_t *pcm, size_t samples)
{
    uint64_t sum = 0U;
    for (size_t i = 0U; i < samples; i++) {
        const int32_t v = (int32_t)pcm[i];
        sum += (uint64_t)(v * v);
    }
    if (samples == 0U) {
        return false;
    }
    const uint64_t mean = sum / (uint64_t)samples;
    /* sqrt 太贵；用均值近似（阈值按 RMS 标定） */
    const uint64_t approx = mean / 1000U;
    return (approx >= (uint64_t)CONFIG_LERO_VOICE_VAD_RMS_THRESHOLD);
}

voice_capture_result_t voice_capture_run(volatile bool *stop)
{
    voice_capture_result_t res;
    (void)memset(&res, 0, sizeof(res));

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = CONFIG_LERO_VOICE_SAMPLE_RATE,
        .channel = CONFIG_LERO_VOICE_CHANNELS,
        .bits_per_sample = 16,
    };
    /* 播放器可能已打开同格式 codec：打开失败时直接读（双工共享） */
    const bool opened = (esp_codec_dev_open(s_codec, &fs) == ESP_CODEC_DEV_OK);
    if (!opened) {
        ESP_LOGD(TAG, "codec already open (player active); reading shared duplex");
    }

    const int64_t start_us = esp_timer_get_time();
    const int64_t timeout_us = (int64_t)CONFIG_LERO_VOICE_LISTEN_TIMEOUT_MS * 1000;
    const int64_t tail_us = (int64_t)CONFIG_LERO_VOICE_VAD_TAIL_MS * 1000;
    const int64_t min_us = (int64_t)CONFIG_LERO_VOICE_MIN_UTTERANCE_MS * 1000;
    int64_t last_speech_us = 0;

    while ((stop == NULL) || !(*stop)) {
        if ((esp_timer_get_time() - start_us) >= timeout_us) {
            break;
        }
        const int got = esp_codec_dev_read(s_codec, s_pcm, FRAME_BYTES);
        if (got <= 0) {
            vTaskDelay(pdMS_TO_TICKS(2U));  /* 无数据：稍候重试 */
            continue;
        }
        const size_t samples = (size_t)got / 2U;
        res.frames++;
        res.bytes += (uint32_t)got;

        const bool speech = s_frame_has_speech(s_pcm, samples);
        if (speech) {
            res.utterance = true;
            last_speech_us = esp_timer_get_time();
        }
        /* 上传（transport 空实现只统计；M9 替换为真实通道） */
        if (res.utterance) {
            (void)voice_transport_send(s_pcm, samples / (size_t)CONFIG_LERO_VOICE_CHANNELS,
                                       CONFIG_LERO_VOICE_SAMPLE_RATE,
                                       CONFIG_LERO_VOICE_CHANNELS);
        }
        /* 端点检测：语音后静音超尾长（且已过最短语句时长）→ 结束 */
        if (res.utterance && (last_speech_us != 0) &&
            ((esp_timer_get_time() - last_speech_us) >= tail_us) &&
            ((esp_timer_get_time() - start_us) >= min_us)) {
            break;
        }
    }

    if (opened) {
        (void)esp_codec_dev_close(s_codec);
    }
    return res;
}

/* ------------------------------------------------------------------------- */
/* 录音：PCM(48k/2ch/16bit) -> WAV（SD 文件 或 PSRAM 内存缓冲）              */
/* path == NULL 时录到静态 PSRAM 缓冲（无 SD 卡可用），录完回填头并自动回放    */
/* ------------------------------------------------------------------------- */

/* 内存录音缓冲：显式放入 PSRAM 的 .ext_ram.bss 段（EXT_RAM_BSS_ATTR；
 * IDF master 已把旧名 EXT_RAM_ATTR 改为此名，commit b5de3ec）。
 * 注意：不能依赖 SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY 的自动放置——
 * 大数组仍会被链接进内部 .dram0.bss 导致 sram_seg 溢出（实测 CI）。
 * 容量 = BUF_KB*1024 字节；@48k/2ch/16bit = 192000 B/s → 秒数上限见下 */
EXT_RAM_BSS_ATTR static uint8_t s_rec_mem_buf[CONFIG_LERO_VOICE_RECORD_MEM_BUF_KB * 1024U];
static size_t s_rec_mem_size;               /* 有效数据长度（含 WAV 头） */

#define VOICE_REC_MEM_BYTES_PER_SEC \
    ((uint32_t)CONFIG_LERO_VOICE_SAMPLE_RATE * CONFIG_LERO_VOICE_CHANNELS * 2U)
#define VOICE_REC_MEM_MAX_SECONDS \
    (((uint32_t)CONFIG_LERO_VOICE_RECORD_MEM_BUF_KB * 1024U) / VOICE_REC_MEM_BYTES_PER_SEC)

/* 逐字节填充 44 字节 RIFF/WAVE 头（MISRA 友好，不用 packed struct） */
static void s_wav_fill_header(uint8_t *hdr, uint32_t sample_rate,
                              uint16_t channels, uint16_t bits,
                              uint32_t data_size)
{
    const uint32_t byte_rate = sample_rate * (uint32_t)channels * (uint32_t)(bits / 8U);
    const uint16_t block_align = (uint16_t)(channels * (bits / 8U));
    const uint32_t riff_size = 36U + data_size;

    (void)memcpy(&hdr[0], "RIFF", 4U);
    hdr[4] = (uint8_t)(riff_size & 0xFFU);
    hdr[5] = (uint8_t)((riff_size >> 8) & 0xFFU);
    hdr[6] = (uint8_t)((riff_size >> 16) & 0xFFU);
    hdr[7] = (uint8_t)((riff_size >> 24) & 0xFFU);
    (void)memcpy(&hdr[8], "WAVE", 4U);
    (void)memcpy(&hdr[12], "fmt ", 4U);
    hdr[16] = 16U;                      /* fmt chunk size = 16 */
    hdr[17] = 0U;
    hdr[18] = 1U;                       /* PCM */
    hdr[19] = 0U;
    hdr[20] = (uint8_t)(channels & 0xFFU);
    hdr[21] = (uint8_t)((channels >> 8) & 0xFFU);
    hdr[22] = (uint8_t)(sample_rate & 0xFFU);
    hdr[23] = (uint8_t)((sample_rate >> 8) & 0xFFU);
    hdr[24] = (uint8_t)((sample_rate >> 16) & 0xFFU);
    hdr[25] = (uint8_t)((sample_rate >> 24) & 0xFFU);
    hdr[26] = (uint8_t)(byte_rate & 0xFFU);
    hdr[27] = (uint8_t)((byte_rate >> 8) & 0xFFU);
    hdr[28] = (uint8_t)((byte_rate >> 16) & 0xFFU);
    hdr[29] = (uint8_t)((byte_rate >> 24) & 0xFFU);
    hdr[30] = (uint8_t)(block_align & 0xFFU);
    hdr[31] = (uint8_t)((block_align >> 8) & 0xFFU);
    hdr[32] = (uint8_t)(bits & 0xFFU);
    hdr[33] = (uint8_t)((bits >> 8) & 0xFFU);
    (void)memcpy(&hdr[36], "data", 4U);
    hdr[40] = (uint8_t)(data_size & 0xFFU);
    hdr[41] = (uint8_t)((data_size >> 8) & 0xFFU);
    hdr[42] = (uint8_t)((data_size >> 16) & 0xFFU);
    hdr[43] = (uint8_t)((data_size >> 24) & 0xFFU);
}

esp_err_t voice_capture_record_run(uint32_t seconds, const char *path,
                                   volatile bool *stop)
{
    if ((seconds == 0U) || (seconds > 600U)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_codec == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    const bool mem_mode = (path == NULL);   /* NULL → 内存模式（无 SD） */
    uint8_t *out_buf = NULL;
    size_t out_cap = 0U;
    FILE *fp = NULL;
    if (mem_mode) {
        if (seconds > (uint32_t)VOICE_REC_MEM_MAX_SECONDS) {
            ESP_LOGE(TAG, "record: %u s exceeds in-RAM buffer (%u s max)",
                     (unsigned)seconds, (unsigned)VOICE_REC_MEM_MAX_SECONDS);
            return ESP_ERR_INVALID_SIZE;
        }
        out_buf = s_rec_mem_buf;
        out_cap = sizeof(s_rec_mem_buf);
    } else {
        esp_err_t err = bsp_sdcard_poll();          /* 确保 SD 已挂载 */
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "record: SD unavailable: %s", esp_err_to_name(err));
            return err;
        }
        /* 确保目标目录存在（取路径最后一个 '/' 之前的目录部分） */
        char dir[96];
        (void)strlcpy(dir, path, sizeof(dir));
        char *slash = strrchr(dir, '/');
        if ((slash != NULL) && (slash != dir)) {
            *slash = '\0';
            (void)mkdir(dir, 0755);
        }
        fp = fopen(path, "wb");
        if (fp == NULL) {
            ESP_LOGE(TAG, "record: open %s failed", path);
            return ESP_FAIL;
        }
    }

    /* 打开 codec IN（与聆听同格式，共享时钟域）；播放器占用时共享双工 */
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = CONFIG_LERO_VOICE_SAMPLE_RATE,
        .channel = CONFIG_LERO_VOICE_CHANNELS,
        .bits_per_sample = 16,
    };
    const bool opened = (esp_codec_dev_open(s_codec, &fs) == ESP_CODEC_DEV_OK);
    if (!opened) {
        ESP_LOGD(TAG, "record: codec already open; reading shared duplex");
    }

    /* 先写占位头，录完回填 */
    uint8_t hdr[44];
    s_wav_fill_header(hdr, CONFIG_LERO_VOICE_SAMPLE_RATE,
                      CONFIG_LERO_VOICE_CHANNELS, 16U, 0U);
    size_t wav_pos = 0U;
    if (mem_mode) {
        (void)memcpy(out_buf, hdr, sizeof(hdr));
        wav_pos = sizeof(hdr);
    } else {
        (void)fwrite(hdr, 1U, sizeof(hdr), fp);
    }

    const int64_t start_us = esp_timer_get_time();
    const int64_t duration_us = (int64_t)seconds * 1000000LL;
    uint32_t data_size = 0U;
    bool io_error = false;

    while ((stop == NULL) || !(*stop)) {
        if ((esp_timer_get_time() - start_us) >= duration_us) {
            break;
        }
        const int got = esp_codec_dev_read(s_codec, s_pcm, FRAME_BYTES);
        if (got <= 0) {
            vTaskDelay(pdMS_TO_TICKS(2U));
            continue;
        }
        if (mem_mode) {
            if ((wav_pos + (size_t)got) > out_cap) {   /* 溢出保护 */
                ESP_LOGE(TAG, "record: in-RAM buffer full");
                io_error = true;
                break;
            }
            (void)memcpy(&out_buf[wav_pos], s_pcm, (size_t)got);
            wav_pos += (size_t)got;
        } else {
            if (fwrite(s_pcm, 1U, (size_t)got, fp) != (size_t)got) {
                ESP_LOGE(TAG, "record: write failed (SD full?)");
                io_error = true;
                break;
            }
        }
        data_size += (uint32_t)got;
    }

    /* 回填 WAV 头（data_size 已知） */
    s_wav_fill_header(hdr, CONFIG_LERO_VOICE_SAMPLE_RATE,
                      CONFIG_LERO_VOICE_CHANNELS, 16U, data_size);
    if (mem_mode) {
        (void)memcpy(out_buf, hdr, sizeof(hdr));        /* 缓冲头部覆写 */
        s_rec_mem_size = sizeof(hdr) + (size_t)data_size;
    } else {
        (void)fseek(fp, 0L, SEEK_SET);
        (void)fwrite(hdr, 1U, sizeof(hdr), fp);
        (void)fclose(fp);
    }

    if (opened) {
        (void)esp_codec_dev_close(s_codec);
    }

    ESP_LOGI(TAG, "record done: %s (%u bytes, %.1f s)",
             mem_mode ? "in-RAM" : path, (unsigned)data_size,
             (double)data_size / (double)VOICE_REC_MEM_BYTES_PER_SEC);
    return io_error ? ESP_FAIL : ESP_OK;
}

esp_err_t voice_capture_get_rec_mem(const uint8_t **buf, size_t *size)
{
    if ((buf == NULL) || (size == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_rec_mem_size == 0U) {
        return ESP_ERR_NOT_FOUND;
    }
    *buf = s_rec_mem_buf;
    *size = s_rec_mem_size;
    return ESP_OK;
}

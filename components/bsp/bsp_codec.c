/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file bsp_codec.c
 * @brief ES8389 audio codec (I2C0, addr 0x20) + I2S playback path.
 *
 * Driver: espressif/esp_codec_dev (>= v1.3.6, official ES8389 support).
 *   I2S (master, pins from bsp_config.h) -> audio_codec_data_if
 *   I2C0 bus handle                        -> audio_codec_ctrl_if
 *   es8389_codec_new()                     -> audio_codec_if
 *   esp_codec_dev_new()                    -> playback/record handle
 *
 * NOTE: MCLK is generated on BSP_I2S_MCLK_GPIO (IO36). The current schematic
 * does not route MCLK to the codec (docs/PLAN.md 2.6 #1); the audio path
 * works once the schematic revision + new PCB are in place.
 *
 * NOTE: 针对 esp_codec_dev 2.x（本工程经 gmf 链解析到 2.0.0-beta3）：
 * es8389_codec_cfg_t 由 v1 的平铺字段改为 audio_hw_*_cfg_t 子配置
 * （sys/adc/dac/pa），工作模式枚举已移除。子配置按官方
 * espressif/esp-audio-dev 仓库（device/include/audio_codec_hw_cfg.h）
 * 语义填写：
 *   - sys_cfg.is_master=false : ESP32 I2S 主模式输出 BCLK/LRCK，codec 从模式
 *   - sys_cfg.no_mclk=true    : **BCLK PIN 模式**（官方驱动支持，REG0x02[7:6]=01）：
 *                               ES8389 内部时钟从 I2S BCLK 派生，**无需外部 MCLK**；
 *                               驱动按 BCLK 频率（fs×bits×2）查 coeff_div 系数表。
 *                               原理图 pin4(MCLK/TDMIN) 与 DACDAT 短接共接
 *                               I2S_DSDIN —— BCLK 模式下 pin4 不参与时钟，无影响
 *   - adc_cfg.digital_mic=false : ES8389 模拟麦克风（MIC1P/MIC1N）
 *   - pa_cfg.pa_pin=-1       : PA（NS4150B×2）由 bsp_amplifier 控制，
 *                              避免驱动与 BSP 双控 IO52
 *   - hw_gain                : PA/DAC 供电与电路增益，仅影响音量 dB 标定
 */

#include <string.h>
#include "esp_log.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "es8389_codec.h"
#include "bsp_config.h"
#include "bsp_i2c.h"
#include "bsp_codec.h"

#define TAG "bsp_codec"

#define CODEC_I2S_PORT         I2S_NUM_0
#define CODEC_MCLK_MULTIPLE    I2S_MCLK_MULTIPLE_256

static esp_codec_dev_handle_t s_codec_dev;
static i2s_chan_handle_t s_i2s_tx;
static i2s_chan_handle_t s_i2s_rx;
static i2c_master_dev_handle_t s_probe;   /* I2C 探测句柄（寄存器调试用） */
static bool s_present;

static esp_err_t s_i2s_init(void)
{
    if (s_i2s_tx != NULL) {
        return ESP_OK;
    }
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(CODEC_I2S_PORT, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;                 /* 静音时清 DMA 残留 */
    /* 播放（TX）+ 录音（RX）双通道：esp_codec_dev 的 data_if 同时持有
     * tx/rx 句柄（audio_codec_i2s_cfg_t），录音经 esp_codec_dev_read 走 RX。 */
    esp_err_t err = i2s_new_channel(&chan_cfg, &s_i2s_tx, &s_i2s_rx);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s new channel failed: %s", esp_err_to_name(err));
        return err;
    }
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(48000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,        /* BCLK PIN 模式（no_mclk=true）：
                                             * ES8389 时钟从 I2S BCLK 派生，
                                             * 无需输出 MCLK（官方驱动支持，
                                             * 见 esp_codec_dev es8389.c） */
            .bclk = BSP_I2S_SCLK_GPIO,
            .ws = BSP_I2S_LRCK_GPIO,
            .dout = BSP_I2S_DSDIN_GPIO,         /* SoC -> ES8389 DAC */
            .din = BSP_I2S_SDOUT_GPIO,          /* ES8389 ADC -> SoC（录音） */
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    std_cfg.clk_cfg.mclk_multiple = CODEC_MCLK_MULTIPLE;
    err = i2s_channel_init_std_mode(s_i2s_tx, &std_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s tx std init failed: %s", esp_err_to_name(err));
        return err;
    }
    err = i2s_channel_init_std_mode(s_i2s_rx, &std_cfg);    /* RX 同格式（共享时钟域） */
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s rx std init failed: %s", esp_err_to_name(err));
        return err;
    }
    err = i2s_channel_enable(s_i2s_tx);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s tx enable failed: %s", esp_err_to_name(err));
        return err;
    }
    err = i2s_channel_enable(s_i2s_rx);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s rx enable failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "i2s ready (sclk=%d ws=%d dout=%d din=%d, no mclk: BCLK PIN mode)",
             (int)BSP_I2S_SCLK_GPIO, (int)BSP_I2S_LRCK_GPIO,
             (int)BSP_I2S_DSDIN_GPIO, (int)BSP_I2S_SDOUT_GPIO);
    return ESP_OK;
}

esp_err_t bsp_codec_init(void)
{
    if (s_codec_dev != NULL) {
        return ESP_OK;
    }

    /* 1. I2C 探测：确认 ES8389 在线（寄存器 0 可读） */
    i2c_master_dev_handle_t probe = NULL;
    esp_err_t err = bsp_i2c_add_device0(BSP_ES8389_I2C_ADDR, BSP_I2C0_FREQ_HZ, &probe);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "add device failed: %s", esp_err_to_name(err));
        return err;
    }
    uint8_t reg = 0x00U;
    uint8_t val = 0U;
    err = i2c_master_transmit_receive(probe, &reg, 1U, &val, 1U, 100);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "codec 0x20 no ACK (%s); 排查 AUD_3V3 供电 / 地址 0x20 / I2C0 上拉",
                 esp_err_to_name(err));
        return err;
    }
    s_present = true;
    s_probe = probe;
    ESP_LOGI(TAG, "ES8389 present at 0x20 (reg0=0x%02X)", (unsigned)val);

    /* 2. I2S 通道（主模式，引脚见 bsp_config.h） */
    err = s_i2s_init();
    if (err != ESP_OK) {
        return err;
    }

    /* 3. 控制接口：I2C0 总线（bsp_config 中 I2C0 即 codec/IMU 总线）。
     *    注：esp_codec_dev 2.x 的 audio_codec_i2c_cfg_t 无 port 成员
     *    （bus_handle 方式已足够），勿加 .port。 */
    i2c_master_bus_handle_t bus = NULL;
    err = bsp_i2c_get_bus0(&bus);
    if (err != ESP_OK) {
        return err;
    }
    audio_codec_i2c_cfg_t i2c_cfg = {
        .addr = BSP_ES8389_I2C_ADDR,
        .bus_handle = bus,
        .clock_speed_hz = (int)BSP_I2C0_FREQ_HZ,
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    if (ctrl_if == NULL) {
        ESP_LOGE(TAG, "i2c ctrl if create failed");
        return ESP_FAIL;
    }

    /* 4. 数据接口：I2S 通道句柄（TX 播放 + RX 录音） */
    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = CODEC_I2S_PORT,
        .rx_handle = s_i2s_rx,
        .tx_handle = s_i2s_tx,
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    if (data_if == NULL) {
        ESP_LOGE(TAG, "i2s data if create failed");
        return ESP_FAIL;
    }

    /* 5. ES8389 codec 接口（esp_codec_dev 2.x 子配置结构，字段语义见
     *    espressif/esp-audio-dev 的 audio_codec_hw_cfg.h，见文件头注释） */
    es8389_codec_cfg_t codec_cfg = {
        .ctrl_if = ctrl_if,
        .gpio_if = audio_codec_new_gpio(),
        .sys_cfg = {
            .is_master = false,     /* ESP32 I2S 主模式 → codec 从模式 */
            .no_mclk = true,        /* BCLK PIN 模式：时钟从 I2S BCLK 派生，
                                     * 无需外部 MCLK（官方驱动支持） */
        },
        .adc_cfg = {
            .digital_mic = false,   /* ES8389 模拟麦克风输入 */
            .label = NULL,
        },
        .dac_cfg = { 0 },           /* 无内部 DAC 参考环回 */
        .pa_cfg = {
            .pa_pin = -1,           /* PA 由 bsp_amplifier 管理（防双控） */
            .pa_active_low = false,
            .hw_gain = {
                .pa_voltage = 5.0f,         /* NS4150B 供电（按实际电路） */
                .codec_dac_voltage = 3.3f,  /* ES8389 DAC 供电 */
                .pa_gain = 0.0f,            /* 电路增益，仅影响音量 dB 标定 */
            },
        },
    };
    const audio_codec_if_t *codec_if = es8389_codec_new(&codec_cfg);
    if (codec_if == NULL) {
        ESP_LOGE(TAG, "es8389_codec_new failed");
        return ESP_FAIL;
    }

    /* 6. 顶层编解码设备（播放 + 录音） */
    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT,
        .codec_if = codec_if,
        .data_if = data_if,
    };
    s_codec_dev = esp_codec_dev_new(&dev_cfg);
    if (s_codec_dev == NULL) {
        ESP_LOGE(TAG, "esp_codec_dev_new failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "codec ready (esp_codec_dev, 0x20)");
    return ESP_OK;
}

bool bsp_codec_is_present(void)
{
    return s_present;
}

esp_codec_dev_handle_t bsp_codec_get_handle(void)
{
    return s_codec_dev;
}

esp_err_t bsp_codec_set_volume(uint8_t volume_pct)
{
    if (s_codec_dev == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    if (volume_pct > 100U) {
        return ESP_ERR_INVALID_ARG;
    }
    if (esp_codec_dev_set_out_vol(s_codec_dev, (int)volume_pct) != ESP_CODEC_DEV_OK) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t bsp_codec_mute(bool mute)
{
    if (s_codec_dev == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    if (esp_codec_dev_set_out_mute(s_codec_dev, mute) != ESP_CODEC_DEV_OK) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t bsp_codec_read_reg(uint8_t reg, uint8_t *val)
{
    if ((val == NULL) || (s_probe == NULL)) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_master_transmit_receive(s_probe, &reg, 1U, val, 1U, 100);
}

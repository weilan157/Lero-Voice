# Lero Voice 上电行为与驱动调试指南

> 配套文档：方案 [PLAN.md](PLAN.md) · 环境搭建 [SETUP.md](SETUP.md)
> 本文档覆盖：上电行为预期、console 命令速查、各驱动调试方法、上板调试顺序。

## 1. 上电行为（app_main 流程，按顺序）

```
Lero Voice 0.1.0 boot (IDF v6.1)
├─ NVS 初始化
├─ 事件队列创建
├─ bsp_init()：I2C0(IO0/1) + I2C1(IO46/47) 总线
│   ├─ [bsp_buttons] buttons ready            ← IO55/56/57 上拉扫描（按下低）
│   ├─ [bsp_power]   power ready (ADC2 ch0/ch1, atten=DB0, cali=..)
│   ├─ [bsp_sdcard]  SD 挂载（轮询）
│   ├─ [bsp_storage] storage mounted（首启自动格式化一次）
│   ├─ [bsp_codec]   ES8389 present @0x20      ← 或 "no ACK" 降级（见 3.1）
│   ├─ [bsp_imu]     QMI8658A found @0x6A / imu ready
│   ├─ [bsp_display] display 720x720 ready (backlight off)
│   └─ [bsp_touch]   touch ready: FT6336U @0x38
├─ diag_init（console `lero>` + 日志落盘 + 快照）
├─ prov_init / ota_service_init / 按键事件接入
├─ player_init（ESP-GMF；codec 缺时降级）
├─ voice_init（采集骨架）
├─ ui_init（LVGL）：
│   ├─ LVGL adapter 初始化（任务 8KB / prio 6）
│   ├─ 注册显示 720×720 RGB565（双缓冲，DOUBLE_FULL）
│   ├─ 注册触摸 FT6336U（失败仅降级）
│   ├─ LVGL 任务启动
│   ├─ 背光 100%
│   └─ lv_demo_benchmark() → 屏幕显示 benchmark 场景 + 右上角 FPS
├─ prov_start（自动连已存 WiFi，无配置进入配网）
└─ 3 个静态任务：net_task / ota_task / sys_task
```

**预期现象**：UART0（CN2 调试口，115200）输出以上日志；屏幕点亮显示 LVGL benchmark 动画与 FPS；按键/触摸可交互。

> 硬件现状要点（PLAN 2.4.9 / 2.6）：
> - 屏幕 **NV3052C 无 SPI 初始化路径** → 依赖模组 OTP 出厂配置，上电即显示
> - ES8389 **BCLK PIN 模式**（no_mclk=true），无需 MCLK 即可出声
> - IMU/触摸/按键为轮询或 GPIO 方式（INT 未接/上拉已确认）

## 2. Console 命令速查

| 命令 | 用途 |
|------|------|
| `periph` | **所有模块 init 状态位图**（ok / FAIL+错误码 / disabled）—— 第一眼判断 |
| `i2c-scan` | 扫描 I2C0/I2C1 全地址，列出 ACK 设备（应见 0x20 / 0x6A·0x68 / 0x38） |
| `reg <bus0\|bus1> <addr7> <reg>` | 任意 I2C 寄存器直读（十六进制）—— 驱动级万能工具 |
| `codec` | ES8389 关键寄存器组 dump（时钟 REG0x02[7:6]、ADC/DAC/模拟） |
| `imu` | QMI8658A accel(mg) / gyro(mdps) / temp(°C) |
| `touch` | FT6336U 状态 + 实时坐标（按下显示 x/y） |
| `power` | 电池/总线电压、充电状态、电量百分比 |
| `sd` | SD 卡状态 + 日志文件清单 |
| `play <path>` / `play-loop` / `play-url <url>` / `play-dl <url>` | 播放 / 循环 / **HTTP 流式播放（无需 SD）** / 下载到 SD 后循环 |
| `stop` / `pause` / `resume` / `vol <0-100>` / `player` | 播放控制 |
| `rec [秒]` / `rec-stop` | 录音（默认 30s → WAV → 自动播放）/ 提前结束 |
| `wifi` / `ota` / `ota-check` / `ota-sd` / `ota-confirm` / `ota-cancel` | 配网与 OTA |
| `voice` / `voice-listen` / `voice-stop` | 语音助手骨架 |
| `log <tag\|*> <level>` | 运行时日志级别（如 `log bsp_codec debug`） |
| `mem` / `tasks` / `err` / `snapshot` / `version` | 系统诊断 |

## 3. 各驱动调试

### 3.1 ES8389 音频
```
codec               → 寄存器 dump；REG0x02[7:6]=01 表示 BCLK PIN 模式 ✓
play audio/test.mp3 / rec / vol 80
```
| 现象 | 排查步骤 |
|------|----------|
| 启动日志 `codec 0x20 no ACK` | ① AUD_3V3 供电（万用表）→ ② 0x20 地址核对 → ③ `i2c-scan` 查 I2C0 总线与上拉 |
| 有 ACK 但无声 | ① `codec` 查时钟源位 ② `log bsp_codec debug`（驱动打印 Clock source）③ 示波器量 IO3/4/5/6（SCLK/LRCK/SDOUT/DSDIN）④ `vol` 与功放（`periph` 查 PA=IO52） |
| 播放报错 | `player` 状态、`sd` 卡与文件路径、文件格式（mp3/wav/flac…） |

### 3.2 QMI8658A IMU
```
imu → 静止时 accel: x≈0 y≈0 z≈1000(mg)；gyro ≈ 0；temp ≈ 室温
```
| 现象 | 排查 |
|------|------|
| not found | 0x6A/0x68 双地址（SA0 悬空）；`i2c-scan` 查总线；供电 |
| 读数异常 | 轴序/量程核对（±4g / ±2048dps）；静止 z 应 ~±1000mg |

### 3.3 FT6336U 触摸
```
touch → pressed x=.. y=..（手指按下时）
```
| 现象 | 排查 |
|------|------|
| not initialized | I2C1（IO46/47）、INT(IO2)、RST(IO48)；`i2c-scan` 查 0x38 |
| 坐标错乱/镜像 | bsp_touch.c 的 swap_xy / mirror_x / mirror_y（旋转适配） |

### 3.4 RGB LCD（NV3052C，720×720）
```
（无专用命令；periph 看状态；上电自动跑 LVGL benchmark）
```
| 现象 | 排查 |
|------|------|
| 黑屏 | ① 背光（`power`/periph，BL=IO54）② **模组 OTP 是否烧录**（无 SPI 初始化路径）③ 时序参数（PCLK 31MHz/消隐，bsp Kconfig）④ `log bsp_display debug` |
| 颜色不对 | RGB565 驱动 18 位面板（高位有效，预期轻微色偏，可接受） |
| 花屏/闪烁 | 消隐期/极性（Kconfig 微调）；实际帧率看 benchmark FPS |

### 3.5 按键（IO55/56/57，上拉，按下低）
```
periph → buttons ok
BTN1 长按=配网 / BTN1 超长按=恢复出厂
BTN2 短按=OTA检查·确认 / BTN2 长按=OTA检查
BTN3 短按=LED / BTN3 长按=语音聆听·录音停止
```

### 3.6 电源监控（ADC2）
```
power → battery: 3xxx mV / bus: 5xxx mV / charging: 0|1 / pct
```
| 现象 | 排查 |
|------|------|
| 电压恒 0/异常 | ADC2 与 Wi-Fi 共存（上板实测项）；分压电阻核对（Kconfig 比率） |

### 3.7 SD / storage
```
sd → 卡容量/剩余/日志文件列表
```
启动日志：`storage mounted: xx KB total`（首启自动格式化一次）；`sdcard` 轮询挂载。

### 3.8 配网 / OTA
```
wifi（状态）· ota（状态）· ota-check（HTTP 检查+下载）· ota-sd（SD 强制）
ota-confirm（确认切换+重启）· ota-cancel（取消）
```

## 4. 上板调试顺序（推荐）

1. 串口看启动日志 → `periph` 全模块状态位图
2. `i2c-scan` → 确认 **0x20 / 0x6A·0x68 / 0x38** 三个设备在线（ES8389 no-ACK 为当前首要待解决项）
3. `imu` → 验证 IMU（静止 z≈1000mg）
4. 屏幕 → benchmark FPS 显示（同时验证显示 + LVGL 全链路）
5. `touch` → 触摸链路
6. `play`/`rec` → 音频（BCLK PIN 模式，无需 MCLK）
7. 配网 / OTA 联调（`wifi` → `ota-check`）

## 5. 常见问题速查

| 问题 | 原因与处理 |
|------|-----------|
| 串口无输出 | CN2 连接（HC-1.25-4P）、波特率 115200、UART0=GPIO58/59 |
| ES8389 no-ACK | AUD_3V3 / 0x20 / I2C0 上拉（见 3.1） |
| 屏幕不亮 | OTP 未烧录 / 背光 / 时序（见 3.4） |
| 触摸无效 | I2C1 / 0x38 / RST 时序（见 3.3） |
| 播放无声 | codec 时钟源位 / I2S 引脚 / 功放（见 3.1） |
| 电池电压异常 | ADC2-WiFi 共存 / 分压（见 3.6） |

# Lero Voice 🔊

> **Lero — Let your voice talk.**
> 开源智能 AI 音箱：带屏幕、立体声喇叭、双麦克风、SD 卡与电池，做你的 AI 助手与智能家居控制中枢 —— 像《钢铁侠》的贾维斯。

## 特性

- 🧠 **AI 语音助手** — ESP32-S31 直连 LLM / ESP Private Agents
- 🏠 **智能家居控制** — MQTT（Home Assistant 等）+ Matter（Apple/Google/Alexa），语音与屏幕双入口
- 🔊 **立体声音箱** — ES8389 + 双 NS4150B（3 W×2），支持 BLE Audio (LC3) 与蓝牙经典播放
- 🖥️ **带屏交互** — RGB 并口 LCD + 电容触摸，LVGL v9 界面（SquareLine Studio 设计）
- 🎙️ **双麦克风** — 模拟双麦输入，为唤醒词与回声消除预留
- 💾 **SD 卡扩展** — 本地音乐（MP3/WAV/FLAC/AAC…）播放 / 录音 / 资源存储 / 本地 OTA 固件
- 🔋 **电池 + Type-C 充电** — 桌面、户外皆可用
- 🔄 **双通道 OTA** — HTTP（GitHub Releases）+ SD 卡本地升级，支持自动回滚

## 硬件平台

| 组件 | 型号 |
|------|------|
| 主控 | ESP32-S31-WROOM-3-N16R16V（RISC-V 双核 320 MHz / Wi-Fi 6 / BLE 5.4） |
| 音频 Codec | ES8389（24-bit，8–96 kHz，DAC SNR 110 dB） |
| 功放 | NS4150B × 2（3 W D 类） |
| 屏幕 | RGB 并口 LCD + 电容触摸（分辨率待确认） |
| 存储 | MicroSD（SDIO 4-bit） |
| 传感器 | QMI8658A 六轴 IMU |
| 电源 | Type-C 充电 + 锂电池（LGS5500EP） |

## 软件平台

| 模块 | 选型 |
|------|------|
| 核心框架 | ESP-IDF **master / 6.2+**（target: `esp32s31`；6.2 未发布暂用 master；v6.0 版本线不含 S31 支持） |
| 音频框架 | ESP-GMF + esp_codec_dev（ES8389 官方驱动，≥ v1.3.6） |
| 音频播放 | esp_audio_simple_player（ESP-GMF，SD 卡 MP3/WAV/FLAC/AAC/AMR/M4A） |
| 显示 | LVGL v9 + esp_lvgl_port + SquareLine Studio |
| 配网 | SmartConfig（ESP-TOUCH v2）+ softAP 兜底 + 微信小程序 |
| 智能家居 | esp-mqtt（Home Assistant）+ ESP-Matter |
| OTA | esp_ghota（HTTP）+ SD 卡本地升级 |
| 调试诊断 | diag（串口 console / 日志落盘 / 屏幕诊断页 / coredump） |
| 代码规范 | **MISRA C:2012 子集**（禁止动态内存分配，FreeRTOS/LVGL 全静态） |

## 环境依赖

### 工具链

| 依赖 | 版本要求 | 说明 |
|------|----------|------|
| 操作系统 | Windows 10/11 · macOS 12+ · Ubuntu 20.04/22.04 | 任一即可 |
| Git | ≥ 2.30 | 克隆 ESP-IDF 与本项目 |
| Python | 3.10+（由 ESP-IDF 安装脚本自动管理） | 无需单独安装 |
| **ESP-IDF** | **master / 6.2+**（esp32s31 最低完整支持线；6.2 未发布暂用 master；**v6.0 无 S31 工具链**） | CMake/Ninja/工具链由 `install.sh` 自动安装 |
| 串口驱动 | CP210x / CH340（按调试器芯片安装） | Windows 下需手动安装 |

> 详细安装步骤、首次配置、VS Code 与常见问题见 [🛠️ 开发环境搭建指南](docs/SETUP.md)。

### 硬件

- Lero Voice 主板（PCB 打样前可用 ESP32-S31 系列开发板先行验证，如 ESP32-S31-Korvo-1）
- USB-C 数据线（烧录 + 充电）
- 5 V/2 A 电源或锂电池
- MicroSD 卡（用于 OTA 与本地音乐测试）

### 组件依赖（managed components，由 `idf_component.yml` 自动拉取）

| 组件 | 版本 | 用途 |
|------|------|------|
| `lvgl/lvgl` | ^9.2.0 | 显示框架 |
| `espressif/esp_lvgl_port` | ^2.3.0 | LVGL 适配层 |
| `espressif/esp_lcd_touch` | latest | 电容触摸 |
| `espressif/esp_codec_dev` | ≥ 1.3.6 | **ES8389 音频驱动** |
| `espressif/esp-gmf` | latest | 音频播放/处理管道 |
| `espressif/esp-mqtt` | latest | MQTT 客户端（智能家居） |
| `espressif/esp-matter` | latest | Matter 设备 |
| `ghota/esp_ghota`（git 依赖） | latest | HTTP OTA |
| `espressif/esp_audio_simple_player` | ^1.0.0~2 | SD 卡音乐播放（ESP-GMF） |
| 自有组件 | — | `components/bsp/`、`components/ota_service/`、`components/provisioning/`、`components/player/`、`components/smarthome/`、`components/voice/` |

## 快速开始

```bash
# 1. 安装 ESP-IDF（master 分支（6.2 未发布）；v6.0 无 esp32s31 支持）
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32s31
./export.sh

# 2. 克隆项目（仓库根即 ESP-IDF 工程）
git clone https://github.com/your-username/Lero-Voice.git
cd Lero-Voice

# 3. 配置
idf.py set-target esp32s31
idf.py menuconfig
#   - Partition Table → Custom partition table CSV → partitions.csv
#   - Bootloader config → App rollback enable (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE)
#   - Flash size → 16 MB

# 4. 编译 / 烧录 / 监视
idf.py build
idf.py -p COM3 flash monitor
```

常用命令：`idf.py erase-flash`（清空，含 NVS）· `idf.py size`（查看占用）· `idf.py -p COM3 monitor`（串口日志，Ctrl+] 退出）· monitor 内输入 `help` 查看调试命令（diag console）· 崩溃后 `idf.py coredump-info` 离线分析

## 配网（简要）

1. 开机进入配网模式（或长按功能键 3 s 主动进入）
2. 手机连接 **2.4G** WiFi（SmartConfig 不支持 5G）
3. 打开微信小程序 → 输入 WiFi 密码 → 点击"配网"
4. 成功：提示音 2 短声 + 屏幕显示"联网成功"；60 s 超时自动转 **softAP 兜底**（AP 名 `LeroVoice-XXXX`，浏览器访问 `192.168.4.1` 配置）
5. 恢复出厂：长按功能键 10 s（擦除 NVS + 格式化 storage）
6. **断线自动重连**：联网后掉线（路由重启等）自动指数退避重连（5 s → 5 min 封顶）

> 详细步骤与固件内部流程见 [docs/PLAN.md 第 4 章](docs/PLAN.md)。

## OTA 升级（双通道）

- **HTTP**：完全跟随 GitHub Releases 最新正式版（**只升不降**），`ota_task` 定时检查（latest 直链），拉取完整元信息 `meta.json`（版本/目标/SHA-256/说明等）→ 校验后下载到非运行槽 → **切换槽与重启由用户确认**（console `ota-confirm` / 功能键 2 短按 / 后续 UI 弹窗与语音，超时默认"稍后"）→ 失败自动回滚
- **SD 卡**：将 `Lero-Voice.bin` + `Lero-Voice.bin.sha256`（可选 `update.json`）放入 SD 卡 `update/` 目录，console 执行 `ota-sd` 触发（后续界面"本地升级"入口）—— **强制模式，可降级**（HTTP 不可降级），断网可用
- 控制台：`ota`（状态）· `ota-check` · `ota-sd` · `ota-confirm` · `ota-cancel`
- **升级不影响配置**：OTA 只写 app 分区，NVS 用户配置与 storage 分区全程不触碰
- 分区表：`factory + ota_0 + ota_1 + storage`（16 MB，见 [docs/PLAN.md 第 8 章](docs/PLAN.md)）

## 智能家居（简要）

- **MQTT**：连接 Home Assistant / 自建 broker，支持 HA MQTT Discovery 自动发现，语音与屏幕双入口控制
- **Matter**：ESP-Matter 接入 Apple Home / Google Home / Alexa
- 配置入口：配网配置页 / 小程序的"智能家居"向导（见 [docs/PLAN.md 第 7 章](docs/PLAN.md)）

## 固件工程结构

```
Lero-Voice/            # 仓库根 = ESP-IDF 工程（target: esp32s31）
├── main/              # app_main: nvs → bsp_init → diag/prov/ota → 静态任务
├── components/
│   ├── bsp/           # 板级支持包（唯一接触硬件的层，bsp_config.h 为适配点）
│   ├── provisioning/  # SmartConfig (ESP-TOUCH v2) + softAP 兜底
│   ├── ota_service/   # 双通道 OTA（HTTP + SD）+ 用户确认 + 回滚自证
│   ├── diag/          # console / 日志落盘 / 快照 / 错误 / coredump
│   ├── player/        # SD 卡音乐播放（ESP-GMF + ES8389）
│   └── voice/         # 语音助手骨架（采集/VAD/上传接口，M9）
├── partitions.csv     # factory + ota_0 + ota_1 + storage + coredump (16 MB)
└── sdkconfig.defaults
```

## 播放 SD 卡音乐

1. 将音频文件放入 SD 卡 `audio/` 目录（支持 mp3 / wav / flac / aac / amr / m4a，按扩展名自动识别格式）
2. 插入 SD 卡（`bsp_sdcard` 轮询挂载）
3. 串口控制台（`idf.py -p COM3 monitor`）操作：

```
play audio/example.mp3    # 开始播放（也支持绝对路径 /sdcard/audio/example.mp3）
play-loop audio/example.mp3  # 循环播放（stop 终止）
play-url https://.../song.mp3  # HTTP 流式播放（无需 SD 卡），循环
pause / resume            # 暂停 / 继续
stop                      # 停止
vol 80                    # 音量 0-100
player                    # 查询播放状态
rec [秒]                  # 录音 N 秒（默认 30）→ WAV → 自动播放
rec-stop                  # 提前结束录音
```

> 播放管线：SD 文件 → ESP-GMF 解码（esp_audio_simple_player）→ PCM → ES8389（esp_codec_dev，I2S 主模式）。录音管线：ES8389 ADC → I2S RX → WAV（/sdcard/record/rec.wav）。✅ **无需 MCLK**：ES8389 用 BCLK PIN 模式（`no_mclk=true`，时钟从 I2S BCLK 派生，见 PLAN 2.6 #1）。

## 开发约定

- **BSP 先行**：`components/bsp/` 为唯一接触硬件外设的层，应用层只调 BSP 接口；换板只改 `bsp_config.h`；**引脚映射见 [docs/PLAN.md 2.4 节](docs/PLAN.md)（已按原理图逐网络核实）**
- **无动态内存**：自有代码禁用 `malloc/free`（MISRA C:2012 规则 21.3）；FreeRTOS 全静态 API，LVGL 使用静态池
- **代码风格**：MISRA C:2012 子集 + AUTOSAR 风格命名；CI 静态检查（cppcheck --addon=misra + gcc -Wall -Wextra -Werror）

## 文档

- [🛠️ 开发环境搭建指南](docs/SETUP.md) — 依赖安装 / 首次配置 / VS Code / 常见问题排查
- [🔧 上电行为与驱动调试指南](docs/DEBUG.md) — 启动流程 / console 命令速查 / 各驱动调试与故障排查 / 上板调试顺序
- [📋 项目完整方案（v3.3）](docs/PLAN.md) — 硬件 / 逐模块引脚表 / 原理图核对记录 / BSP / 代码规范 / 配网详细步骤 / 智能家居 / OTA 双通道 / 分区表 / 调试诊断 / 里程碑 / 资料清单
- [原理图（2026-08-25）](docs/SCH_Schematic1_1_2026-08-25.pdf)
- [ESP32-S31-WROOM-3 数据手册（CN）](docs/esp32-s31-wroom-3_datasheet_cn.pdf)
- [屏幕规格书与芯片手册](docs/01-规格书与芯片手册/) — ZJY400-8532ACT 模组规格书 + NV3052C 驱动 IC 数据手册（2026-08-27 归档）

## 项目进度

- [x] 硬件方案选型
- [x] 原理图设计
- [ ] 原理图修订（SD_DET 连接、核对 IO48/49 标注；MCLK 已由 BCLK PIN 模式解决）
- [ ] PCB Layout
- [x] 工程骨架 + 代码规范（MISRA / 无动态内存）（main + bsp + components）
- [x] BSP 框架（bsp/ 十个模块 + 故障位图）
- [x] 配网框架（SmartConfig + softAP 兜底 + NVS 保存）
- [x] OTA 双通道框架（HTTP 只升不降 + SD 强制可降级 + 用户确认 + 回滚）
- [x] 调试诊断框架（diag: console / 日志落盘 / 快照 / 故障位图 / coredump）
- [x] SD 卡音乐播放（esp_audio_simple_player + ES8389，console 控制）
- [x] 语音助手骨架（voice 组件：采集 / VAD / 上传接口 / 唤醒占位）
- [x] CI（.github/workflows/build.yml：构建 / 体积检查 / meta.json / Release / 静态分析）
- [ ] 显示 / 音频录音 / 智能家居 / 语音助手（唤醒+云端）/ 外壳 / 整机联调（BSP 驱动需上板实测校准）

## 许可证

[MIT](LICENSE)

---

**Lero Voice — 让每一声呼唤，都有回应。** 🔊

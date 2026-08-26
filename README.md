# Lero Voice 🔊

> **Lero — Let your voice talk.**
> 开源智能 AI 音箱：带屏幕、立体声喇叭、双麦克风、SD 卡与电池，做你的 AI 助手与智能家居控制中枢 —— 像《钢铁侠》的贾维斯。

## 特性

- 🧠 **AI 语音助手** — ESP32-S31 直连 LLM / ESP Private Agents
- 🏠 **智能家居控制** — MQTT（Home Assistant 等）+ Matter（Apple/Google/Alexa），语音与屏幕双入口
- 🔊 **立体声音箱** — ES8389 + 双 NS4150B（3 W×2），支持 BLE Audio (LC3) 与蓝牙经典播放
- 🖥️ **带屏交互** — RGB 并口 LCD + 电容触摸，LVGL v9 界面（SquareLine Studio 设计）
- 🎙️ **双麦克风** — 模拟双麦输入，为唤醒词与回声消除预留
- 💾 **SD 卡扩展** — 本地音乐 / 录音 / 资源存储 / 本地 OTA 固件
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
| 核心框架 | ESP-IDF **v6.0+**（target: `esp32s31`） |
| 音频框架 | ESP-GMF + esp_codec_dev（ES8389 官方驱动，≥ v1.3.6） |
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
| **ESP-IDF** | **v6.0+**（esp32s31 支持的最低主线版本） | CMake/Ninja/工具链由 `install.sh` 自动安装 |
| 串口驱动 | CP210x / CH340（按调试器芯片安装） | Windows 下需手动安装 |

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
| 自有组件 | — | `bsp/`、`components/ota_service/`、`components/provisioning/`、`components/smarthome/`、`components/voice/` |

## 快速开始

```bash
# 1. 安装 ESP-IDF v6.0+（首次）
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32s31
./export.sh

# 2. 克隆项目
git clone https://github.com/your-username/Lero-Voice.git
cd Lero-Voice/firmware

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
5. 恢复出厂：长按功能键 10 s

> 详细步骤与固件内部流程见 [docs/PLAN.md 第 4 章](docs/PLAN.md)。

## OTA 升级（双通道）

- **HTTP**：完全跟随 GitHub Releases 最新正式版（**只升不降**），`ota_task` 定时检查（latest 直链），拉取完整元信息 `meta.json`（版本/目标/SHA-256/说明等）→ 校验后下载到非运行槽 → **切换槽与重启由用户确认**（界面弹窗或语音指令，超时默认"稍后"）→ 失败自动回滚
- **SD 卡**：将 `lero_app.bin` + `lero_app.bin.sha256`（可选 `update.json`）放入 SD 卡 `update/` 目录，插卡或界面点击"本地升级"即可 —— **强制模式，可降级**（HTTP 不可降级），断网可用
- **升级不影响配置**：OTA 只写 app 分区，NVS 用户配置与 storage 分区全程不触碰
- 分区表：`factory + ota_0 + ota_1 + storage`（16 MB，见 [docs/PLAN.md 第 8 章](docs/PLAN.md)）

## 智能家居（简要）

- **MQTT**：连接 Home Assistant / 自建 broker，支持 HA MQTT Discovery 自动发现，语音与屏幕双入口控制
- **Matter**：ESP-Matter 接入 Apple Home / Google Home / Alexa
- 配置入口：配网配置页 / 小程序的"智能家居"向导（见 [docs/PLAN.md 第 7 章](docs/PLAN.md)）

## 开发约定

- **BSP 先行**：`firmware/bsp/` 为唯一接触硬件外设的层，应用层只调 BSP 接口；换板只改 `bsp_config.h`；**引脚映射见 [docs/PLAN.md 2.4 节](docs/PLAN.md)（已按原理图逐网络核实）**
- **无动态内存**：自有代码禁用 `malloc/free`（MISRA C:2012 规则 21.3）；FreeRTOS 全静态 API，LVGL 使用静态池
- **代码风格**：MISRA C:2012 子集 + AUTOSAR 风格命名；CI 静态检查（cppcheck --addon=misra + gcc -Wall -Wextra -Werror）

## 文档

- [📋 项目完整方案（v3.3）](docs/PLAN.md) — 硬件 / 逐模块引脚表 / 原理图核对记录 / BSP / 代码规范 / 配网详细步骤 / 智能家居 / OTA 双通道 / 分区表 / 调试诊断 / 里程碑 / 资料清单
- [原理图（2026-08-25）](docs/SCH_Schematic1_1_2026-08-25.pdf)
- [ESP32-S31-WROOM-3 数据手册（CN）](docs/esp32-s31-wroom-3_datasheet_cn.pdf)

## 项目进度

- [x] 硬件方案选型
- [x] 原理图设计
- [ ] 原理图修订（补 MCLK / SD_DET、核对 IO48/49 标注）
- [ ] PCB Layout
- [x] 工程骨架 + 代码规范（MISRA / 无动态内存）（main + bsp + components）
- [x] BSP 框架（bsp/ 十个模块 + 故障位图）
- [x] 配网框架（SmartConfig + softAP 兑底 + NVS 保存）
- [x] OTA 双通道框架（HTTP 只升不降 + SD 强制可降级 + 用户确认 + 回滚）
- [x] 调试诊断框架（diag: console / 日志落盘 / 快照 / 故障位图 / coredump）
- [ ] 显示 / 音频 / 智能家居 / 语音助手 / 外壳 / 整机联调（BSP 驱动需上板实测校准）

## 许可证

[MIT](LICENSE)

---

**Lero Voice — 让每一声呼唤，都有回应。** 🔊

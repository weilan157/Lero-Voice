# Lero Voice 开发环境搭建指南

> 面向新开发者的完整环境准备步骤（Windows / macOS / Linux）。
> 硬件前提：ESP32-S31 目标板（或参考板 ESP32-S31-Korvo-1 / ESP32-S31-Function-CoreBoard-1）。

---

## 1. 环境依赖总览

| 依赖 | 版本要求 | 说明 |
|------|----------|------|
| 操作系统 | Windows 10/11 · macOS 12+ · Ubuntu 20.04/22.04 | 任一即可 |
| Git | ≥ 2.30 | 克隆 ESP-IDF 与本项目 |
| Python | 3.10+（由 ESP-IDF 安装脚本自动管理） | 无需单独安装 |
| **ESP-IDF** | **v6.0+**（esp32s31 支持的最低主线版本） | CMake/Ninja/工具链由 `install.sh` 自动安装 |
| 串口驱动 | CP210x / CH340（按调试器芯片安装） | Windows 下需手动安装 |
| 组件依赖 | 由 `idf_component.yml` 自动拉取（见下） | 无需手动下载 |

> 固件源码位于**仓库根**（仓库根即 ESP-IDF 工程），没有嵌套目录。

## 2. 安装 ESP-IDF v6.0+

### Windows

```bat
:: 方式一：官方安装器（推荐新手）
:: 下载 esp-idf-tools-setup 并选择 v6.0 与 esp32s31 目标
:: 方式二：Git Bash 手动安装
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.ps1 esp32s31
./export.ps1
```

### macOS / Linux

```bash
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32s31
./export.sh          # 每次新终端执行，或写入 ~/.bashrc
```

> 安装较慢（下载工具链 + Python 环境），网络受限时可设置镜像：
> `export IDF_GITHUB_ASSETS="dl.espressif.cn/github_assets"`

## 3. 克隆项目并首次配置

```bash
git clone https://github.com/your-username/Lero-Voice.git
cd Lero-Voice

idf.py set-target esp32s31

# 关键配置（已由 sdkconfig.defaults 预置，必要时复核）
idf.py menuconfig
#   - Partition Table → Custom partition table CSV → partitions.csv
#   - Bootloader config → App rollback enable (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE)
#   - Flash size → 16 MB (CONFIG_ESPTOOLPY_FLASHSIZE_16MB)
#   - Lero Voice OTA Service → LERO_OTA_HTTP_META_URL ← 改为你的 Releases 直链
#   - Lero Voice Provisioning → LERO_PROV_PROBE_URL（按网络环境）
```

**组件依赖自动拉取**：`idf.py build` 首次执行时按根目录 `idf_component.yml` 自动下载
`esp_codec_dev`（ES8389 驱动）与 `esp_audio_simple_player`（ESP-GMF 播放器）到
`managed_components/`（该目录已 gitignore，可随时删除重建：`idf.py reconfigure`）。

## 4. 构建 / 烧录 / 监控

```bash
idf.py build                      # 编译
idf.py -p COM3 flash monitor      # 烧录 + 串口监控（Windows 端口示例）
idf.py -p /dev/ttyUSB0 flash monitor   # Linux/macOS 端口示例
```

| 常用命令 | 作用 |
|----------|------|
| `idf.py erase-flash` | 清空 Flash（含 NVS，慎用） |
| `idf.py size` | 查看各分区占用 |
| `idf.py reconfigure` | 重新解析组件/配置 |
| `idf.py coredump-info` | 解析崩溃转储（coredump 分区） |
| `idf.py flash monitor` 内 `help` | 查看 diag console 命令（见 PLAN 3.8） |

## 5. VS Code 开发环境

### 5.1 推荐扩展

| 扩展 | 用途 |
|------|------|
| `espressif.esp-idf-extension` | 官方 ESP-IDF 扩展（构建/烧录/监控/调试/CMake 集成） |
| `espressif.esp-idf-web` | Web 版辅助（可选） |

### 5.2 仓库已提交的配置（`.vscode/`）

- `extensions.json` — 推荐扩展清单（打开项目时自动提示安装）
- `settings.json` — 目标芯片 `esp32s31`、IDF 路径取 `$IDF_PATH`、UART 烧录
- `c_cpp_properties.json` — 头文件路径（components/、main/、IDF、build/config 的 sdkconfig.h）
- `tasks.json` — 任务：`idf: build`（默认构建）/ `idf: flash & monitor`（弹窗输入串口）/ `idf: menuconfig` / `idf: erase flash`

> 使用前提：终端已执行 `export.sh`（或扩展的 `idf.espIdfPath` 指向 IDF 安装目录）。
> 首次打开后运行 **ESP-IDF: Select port** 选择串口。

### 5.3 开发容器（可选，`.devcontainer/`）

仓库附带 ESP-IDF 容器定义（含 vscode 扩展）。使用 VS Code 打开后选择
**Reopen in Container** 即可获得隔离的完整环境（QEMU 模式 `--privileged`）。

## 6. 常见问题排查

| 问题 | 处理 |
|------|------|
| `idf.py` 命令不存在 | 未执行 `export.sh`（Windows 用 `export.ps1` / 官方安装器快捷方式） |
| 找不到串口 | 安装 CP210x/CH340 驱动；Linux 将用户加入 `dialout` 组后重新登录 |
| 烧录报权限错误 | `sudo chmod 666 /dev/ttyUSB0` 或配置 udev 规则 |
| 组件下载失败 | 检查网络；删除 `managed_components/` 后 `idf.py reconfigure` 重试 |
| 构建报 `partition_table_sha256.h` 缺失 | 正常现象——由构建脚本自动生成（见 ota_service/CMakeLists.txt） |
| 分区表不匹配 | `idf.py erase-flash` 后重烧（分区表变更需全量烧录，见 PLAN 8.6 #1） |
| 设备反复重启 | 串口监控看日志；`err` 命令查看复位原因；`idf.py coredump-info` 分析崩溃 |
| 音频无声 | 检查原理图 MCLK 接线（PLAN 2.6 #1）；console `periph` 看 codec 状态 |

## 7. 相关文档

- 项目方案（引脚/BSP/OTA/配网/调试）：[PLAN.md](PLAN.md)
- 快速开始（精简版）：[README.md](../README.md)

# Lero Voice - ESP-IDF top level build script
#
# NOTE: MINIMAL_BUILD is intentionally NOT used here. The bsp / ota_service /
# provisioning / diag components (and their dependencies such as esp_lcd,
# esp_driver_*, fatfs, spiffs, esp_http_client) must all participate in the
# build so that the framework described in docs/PLAN.md is complete.
cmake_minimum_required(VERSION 3.22)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Firmware version. The HTTP OTA channel compares this version (semantic
# versioning, see docs/PLAN.md section 8) against meta.json "version".
set(PROJECT_VER "0.1.0")

project(Lero-Voice)

# 兼容 espressif/button 4.2 在 esp32s31 上的编译：S31 新一代 SAR ADC
# 移除了 SOC_ADC_CHANNEL_NUM(unit) 宏（通道概念重构为差分/共享电源），
# 而 button_adc.c 仍引用它导致编译失败（IDF master 滚动更新后暴露）。
# 本项目按键为自研 bsp_buttons（GPIO 直连），不使用 button 的 ADC 按键
# 功能——提供该宏定义（S31 ADC1/ADC2 各 8 通道）使组件通过编译。
# 仅对 SOC_ADC_CHANNEL_NUM 缺失的芯片生效，不影响其它目标。
if(IDF_TARGET STREQUAL "esp32s31")
    add_compile_definitions("SOC_ADC_CHANNEL_NUM(x)=8")
endif()

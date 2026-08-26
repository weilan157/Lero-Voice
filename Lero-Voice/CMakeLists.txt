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

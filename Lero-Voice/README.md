# Lero Voice (ESP-IDF firmware)

ESP32-S31 based smart speaker firmware. See the repository root README and
docs/PLAN.md for the full design (pin map, BSP, provisioning, OTA, diag,
milestones).

Project layout:

    Lero-Voice/
    ├── main/            # app_main: nvs -> bsp_init -> diag/prov/ota -> static tasks
    ├── bsp/             # board support package (only layer touching hardware)
    ├── components/
    │   ├── provisioning # SmartConfig (ESP-TOUCH v2) + softAP fallback
    │   ├── ota_service  # dual channel OTA (HTTP + SD) with user confirmation
    │   └── diag         # console / log ring to SD / snapshot / errors
    ├── partitions.csv   # factory + ota_0 + ota_1 + storage (16 MB)
    └── sdkconfig.defaults

Build:

    idf.py set-target esp32s31
    idf.py build
    idf.py -p COM3 flash monitor

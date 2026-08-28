idf_component_register(
    SRCS
        "diag.c"
        "diag_log.c"
        "diag_errors.c"
        "diag_panic.c"
        "diag_snapshot.c"
        "diag_console.c"
    INCLUDE_DIRS "."
    REQUIRES bsp provisioning ota_service player voice bt_audio
    PRIV_REQUIRES
        console
        esp_driver_i2c
        esp_app_format
        heap
        esp_system
        esp_stdio
        esp_timer
        nvs_flash
        esp_common
        log
        freertos
)


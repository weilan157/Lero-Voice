idf_component_register(
    SRCS
        "ui.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_lvgl_adapter
    PRIV_REQUIRES
        esp_common
        esp_system
        log
        freertos
)

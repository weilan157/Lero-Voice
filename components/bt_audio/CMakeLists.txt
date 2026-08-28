idf_component_register(
    SRCS
        "bt_audio.c"
    INCLUDE_DIRS "."
    REQUIRES bsp player esp_codec_dev bt
    PRIV_REQUIRES
        esp_common
        log
        freertos
)

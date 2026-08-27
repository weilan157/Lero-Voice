idf_component_register(
    SRCS
        "voice.c"
        "voice_capture.c"
        "voice_transport.c"
        "voice_wake.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_codec_dev player
    PRIV_REQUIRES
        esp_timer
        esp_common
        esp_system
        log
        freertos
)

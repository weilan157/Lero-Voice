idf_component_register(
    SRCS
        "player.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_audio_simple_player esp_codec_dev
    PRIV_REQUIRES
        esp_common
        esp_http_client
        esp_system
        log
        freertos
)

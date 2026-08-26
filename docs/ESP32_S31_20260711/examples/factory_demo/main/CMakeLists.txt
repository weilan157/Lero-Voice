idf_component_register(
    SRCS
        "main.c"
        "app_ui.c"
        "lunar.c"
        "font_clock_40.c"
        "font_clock_60.c"
        "font_wc_cn_16.c"
        "font_wc_cn_18.c"
        "font_wc_cn_20.c"
        "font_wc_cn_22.c"
        "font_wc_cn_24.c"
        "font_wc_cn_32.c"
        "font_wc_emoji_24.c"
        "font_wc_emoji_46.c"
        "font_wc_emoji_56.c"
        "font_wc_emoji_100.c"
        "net_wifi.c"
        "net_weather.c"
        "factory_demo_audio.c"
        "factory_demo_camera.c"
        "factory_demo_usb_hid.c"
    INCLUDE_DIRS "."
    REQUIRES esp32_s31_korvo esp_timer heap esp_driver_ppa esp_lvgl_adapter esp_lv_decoder
        esp_wifi esp_netif esp_event nvs_flash esp_http_client esp-tls mbedtls lwip
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE=1)

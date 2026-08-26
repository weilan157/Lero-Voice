idf_component_register(
    SRCS "esp32_s31_korvo.c" "esp32_s31_adc_calibration.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "priv_include"
    REQUIRES esp_adc esp_driver_gpio esp_driver_i2c esp_driver_i2s esp_hw_support esp_lcd
             esp_lvgl_adapter button espressif__esp_lcd_touch_gt1151 led_strip
             esp_driver_cam esp_cam_sensor esp_sccb_intf esp_video
             esp_driver_sdmmc fatfs sdmmc)

idf_component_set_property(${COMPONENT_NAME} WHOLE_ARCHIVE TRUE)

idf_component_register(
    SRCS
        "provisioning.c"
        "prov_nvs.c"
        "prov_smartconfig.c"
        "prov_softap.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        bsp
        esp_wifi
        esp_netif
        esp_event
        nvs_flash
        esp_http_client
        lwip
        esp_timer
        esp_common
        esp_system
        log
        freertos
)


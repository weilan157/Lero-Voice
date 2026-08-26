idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES bsp provisioning ota_service diag player
    PRIV_REQUIRES nvs_flash
)

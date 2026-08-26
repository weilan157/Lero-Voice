idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES bsp provisioning ota_service diag player voice
    PRIV_REQUIRES nvs_flash
)

idf_component_register(
    SRCS
        "ota_service.c"
        "ota_meta.c"
        "ota_http.c"
        "ota_sd.c"
        "ota_verify.c"
        "ota_partition.c"
        "ota_state.c"
    INCLUDE_DIRS "."
    REQUIRES bsp
    PRIV_REQUIRES
        app_update
        esp_app_format
        esp_partition
        esp_http_client
        mbedtls
        nvs_flash
        esp_timer
        esp_common
        esp_system
        log
        freertos
)

# Generate partition_table_sha256.h at build time (docs/PLAN.md 8.7 / 8.11 #7).
# The HTTP OTA channel compares this value with meta.json
# "partition_table_sha256" and refuses updates that would change the layout.
set(LERO_PT_CSV ${CMAKE_SOURCE_DIR}/partitions.csv)
set(LERO_PT_SHA_HEADER ${CMAKE_BINARY_DIR}/partition_table_sha256.h)
add_custom_command(
    OUTPUT ${LERO_PT_SHA_HEADER}
    COMMAND ${CMAKE_COMMAND}
        -DSRC=${LERO_PT_CSV}
        -DOUT=${LERO_PT_SHA_HEADER}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/gen_pt_sha.cmake
    DEPENDS ${LERO_PT_CSV}
    VERBATIM)
target_sources(${COMPONENT_LIB} PRIVATE ${LERO_PT_SHA_HEADER})
target_include_directories(${COMPONENT_LIB} PUBLIC ${CMAKE_BINARY_DIR})


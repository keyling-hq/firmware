idf_component_register(
    SRCS "ota_update.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES app_update mbedtls
)

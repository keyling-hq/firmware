idf_component_register(
    SRCS "keystore.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES bootloader_support efuse esp_hw_support esp_security mbedtls nvs_flash
)

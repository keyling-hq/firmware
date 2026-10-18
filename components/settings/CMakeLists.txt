idf_component_register(
    SRCS "settings.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_hw_support nvs_flash
)

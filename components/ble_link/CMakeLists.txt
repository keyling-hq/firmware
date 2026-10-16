idf_component_register(
    SRCS "ble_link.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES bt esp_timer esp_hw_support
)

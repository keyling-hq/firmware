idf_component_register(
    SRCS "status_led.c" "status_led_pattern.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES bsp
)

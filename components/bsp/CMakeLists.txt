idf_component_register(
    SRCS "bsp_display.c" "bsp_button.c" "st7735/esp_lcd_st7735.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "st7735"
    PRIV_REQUIRES esp_lcd esp_driver_spi esp_driver_gpio esp_driver_ledc
)

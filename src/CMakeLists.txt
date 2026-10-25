idf_component_register(
    SRCS "main.c" "app.c"
    PRIV_REQUIRES ble_link bootloader_support bsp canokey datafs efuse espcoredump fw_info keystore keymap logbuf nvs_flash ota_update protocol settings spi_flash status_led typer ui usb_hid
)

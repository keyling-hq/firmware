idf_component_register(
    SRCS "main.c" "app.c"
    PRIV_REQUIRES ble_link bsp canokey datafs espcoredump fw_info keystore keymap nvs_flash ota_update protocol settings status_led typer ui usb_hid
)

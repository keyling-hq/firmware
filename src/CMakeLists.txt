idf_component_register(
    SRCS "main.c" "app.c"
    PRIV_REQUIRES ble_link bsp keymap nvs_flash settings status_led typer ui usb_hid
)

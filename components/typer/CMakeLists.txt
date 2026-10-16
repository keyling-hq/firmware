idf_component_register(
    SRCS "typer.c"
    INCLUDE_DIRS "include"
    REQUIRES keymap esp_ringbuf
    PRIV_REQUIRES usb_hid
)

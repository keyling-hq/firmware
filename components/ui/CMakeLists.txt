idf_component_register(
    SRCS "ui.c" "ui_input.c" "ui_settings.c" "ui_jiggler.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES bsp fw_info keymap keystore settings usb_hid
)

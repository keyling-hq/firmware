set(srcs "ui.c" "ui_input.c" "ui_settings.c" "ui_jiggler.c" "ui_profiles.c"
    "ui_theme_candy.c" "ui_theme_device.c")
if(CONFIG_UI_THEME_EXAMPLE)
    list(APPEND srcs "ui_theme_example.c")
endif()
if(CONFIG_UI_APP_EXAMPLE)
    list(APPEND srcs "ui_app_example.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES bsp fw_info keymap keystore settings usb_hid
)

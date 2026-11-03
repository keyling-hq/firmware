set(srcs "ui.c" "ui_nav.c" "ui_input.c" "ui_settings.c" "ui_sites.c" "ui_site_table.c" "ui_theme_candy.c" "ui_theme_device.c")
if(CONFIG_UI_APP_JIGGLER)
    list(APPEND srcs "ui_jiggler.c")
endif()
if(CONFIG_UI_APP_PROFILES)
    list(APPEND srcs "ui_profiles.c")
endif()
if(CONFIG_UI_APP_DRIVE)
    list(APPEND srcs "ui_drive.c")
endif()
if(CONFIG_UI_APP_EXAMPLE)
    list(APPEND srcs "ui_app_example.c")
endif()
if(CONFIG_UI_THEME_EXAMPLE)
    list(APPEND srcs "ui_theme_example.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES bsp fw_info keymap keystore settings status_led usb_hid
)

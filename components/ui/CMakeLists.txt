idf_component_register(
    SRCS "ui.c" "ui_input.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES bsp keymap settings
)

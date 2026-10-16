#pragma once

#include "esp_err.h"
#include "iot_button.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_ACTION_TYPE_TEST,
    UI_ACTION_KEYBOARD_LAYOUT,
    UI_ACTION_PAIRING,
    UI_ACTION_ABOUT,
} ui_action_t;

/** Called from the LVGL task: must not block. */
typedef void (*ui_action_cb_t)(ui_action_t action);

/** Build the UI and attach the button as LVGL input. Call after bsp_display_start(). */
esp_err_t ui_start(button_handle_t btn, ui_action_cb_t action_cb);

#ifdef __cplusplus
}
#endif

#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "iot_button.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_ACTION_TYPE_TEST,
    UI_ACTION_SET_LAYOUT,       /* arg: keymap layout index */
    UI_ACTION_SET_SPEED,        /* arg: settings speed preset index */
    UI_ACTION_ABORT_TYPING,
    UI_ACTION_FORGET_PHONE,
} ui_action_t;

/** Called from the LVGL task. Settings changes must be applied before it returns. */
typedef void (*ui_action_cb_t)(ui_action_t action, int arg);

/** Build the UI and attach the button as LVGL input. Call after bsp_display_start(). */
esp_err_t ui_start(button_handle_t btn, ui_action_cb_t action_cb);

/* Thread safe, callable from any task once ui_start() has returned. */
void ui_set_usb_state(bool mounted);
void ui_show_typing(bool typing);

#ifdef __cplusplus
}
#endif

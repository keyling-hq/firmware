#pragma once

#include <stdbool.h>
#include <stdint.h>
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
    UI_ACTION_PAIR_NEW,
    UI_ACTION_CANCEL_PAIRING,
    UI_ACTION_FORGET_PHONE,
} ui_action_t;

typedef enum {
    UI_BLE_ADVERTISING,         /* bonded phone not connected */
    UI_BLE_UNPAIRED,
    UI_BLE_PAIRING,
    UI_BLE_SECURING,
    UI_BLE_CONNECTED,
} ui_ble_state_t;

/** Called from the LVGL task. Settings changes must be applied before it returns. */
typedef void (*ui_action_cb_t)(ui_action_t action, int arg);

/** Build the UI and attach the button as LVGL input. Call after bsp_display_start(). */
esp_err_t ui_start(button_handle_t btn, ui_action_cb_t action_cb);

/*
 * Thread safe, callable from any task once ui_start() has returned. The show functions wake
 * the screen; the highest priority active one is displayed: passkey, typing, pairing window.
 */
void ui_set_usb_state(bool mounted);
void ui_set_ble_state(ui_ble_state_t state);
void ui_show_typing(bool typing);
void ui_show_pairing(bool open);
void ui_show_passkey(uint32_t passkey, bool show);

#ifdef __cplusplus
}
#endif

#pragma once

#include <stdbool.h>
#include "lvgl.h"
#include "iot_button.h"

/**
 * Keypad indev fed by button gestures:
 *   click             -> LV_KEY_NEXT pressed and released
 *   double click      -> LV_KEY_PREV + LV_KEY_ESC
 *   hold              -> LV_KEY_ENTER pressed once the hold is recognised, released on release,
 *                        so widgets get PRESSED, PRESSING, RELEASED as with a real key
 * While the screen is off, the first gesture only wakes it.
 * Must be called with the LVGL lock held.
 */
lv_indev_t *ui_input_create(button_handle_t btn);

/* Implemented in ui.c */
bool ui_screen_is_on(void);
void ui_screen_wake(void);

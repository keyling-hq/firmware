#pragma once

#include <stdbool.h>
#include "lvgl.h"
#include "iot_button.h"

/* Virtual key that only turns the screen on. Outside the LV_KEY_* range. */
#define UI_KEY_WAKE     0x1000

/**
 * Keypad indev fed by discrete button gestures. Each gesture is a separate virtual key:
 *   click -> LV_KEY_NEXT, long press -> LV_KEY_ENTER, double click -> LV_KEY_PREV + LV_KEY_ESC
 * While the screen is off, the first gesture only wakes it.
 * Must be called with the LVGL lock held.
 */
lv_indev_t *ui_input_create(button_handle_t btn);

/* Implemented in ui.c */
bool ui_screen_is_on(void);
void ui_screen_wake(void);

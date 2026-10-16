#pragma once

#include "lvgl.h"
#include "iot_button.h"

/**
 * Keypad indev fed by discrete button gestures. Each gesture is a separate virtual key:
 *   click -> LV_KEY_NEXT, long press -> LV_KEY_ENTER, double click -> LV_KEY_ESC
 * Must be called with the LVGL lock held.
 */
lv_indev_t *ui_input_create(button_handle_t btn);

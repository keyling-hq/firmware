#pragma once

#include <stdint.h>
#include "esp_err.h"

typedef struct button_dev_t *button_handle_t;
typedef void (*button_cb_t)(void *button_handle, void *usr_data);

typedef enum {
    BUTTON_PRESS_DOWN = 0,
    BUTTON_PRESS_UP,
    BUTTON_PRESS_REPEAT,
    BUTTON_PRESS_REPEAT_DONE,
    BUTTON_SINGLE_CLICK,
    BUTTON_DOUBLE_CLICK,
    BUTTON_MULTIPLE_CLICK,
    BUTTON_LONG_PRESS_START,
    BUTTON_LONG_PRESS_HOLD,
    BUTTON_LONG_PRESS_UP,
    BUTTON_PRESS_END,
    BUTTON_EVENT_MAX,
    BUTTON_NONE_PRESS,
} button_event_t;

typedef union {
    struct long_press_t {
        uint16_t press_time;
    } long_press;
    struct multiple_clicks_t {
        uint16_t clicks;
    } multiple_clicks;
} button_event_args_t;

esp_err_t iot_button_register_cb(button_handle_t btn_handle, button_event_t event, button_event_args_t *event_args,
                                 button_cb_t cb, void *usr_data);
uint8_t iot_button_get_repeat(button_handle_t btn_handle);

/*
 * The button component's events, from a pressed/released level and the time: PRESS_DOWN, then the
 * repeat count goes up (PRESS_REPEAT from the second press of a sequence), LONG_PRESS_START once
 * held for BSP_BUTTON_LONG_MS, PRESS_UP on release, PRESS_END when no press follows within
 * BSP_BUTTON_SHORT_MS.
 */
#include "bsp/tdongle.h"
#include "sim.h"

static struct {
    button_cb_t cb[BUTTON_EVENT_MAX];
    void *usr[BUTTON_EVENT_MAX];
    bool pressed;
    bool in_sequence;
    bool long_fired;
    uint8_t repeat;
    uint32_t press_ms;
    uint32_t release_ms;
} s_btn;

button_handle_t sim_button_handle(void)
{
    return (button_handle_t)&s_btn;
}

esp_err_t iot_button_register_cb(button_handle_t btn, button_event_t event, button_event_args_t *args,
                                 button_cb_t cb, void *usr_data)
{
    (void)args;
    if (event >= BUTTON_EVENT_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    s_btn.cb[event] = cb;
    s_btn.usr[event] = usr_data;
    return ESP_OK;
}

uint8_t iot_button_get_repeat(button_handle_t btn)
{
    return s_btn.repeat;
}

static void emit(button_event_t event)
{
    if (s_btn.cb[event]) {
        s_btn.cb[event](sim_button_handle(), s_btn.usr[event]);
    }
}

void sim_button_set(bool pressed, uint32_t now_ms)
{
    if (pressed == s_btn.pressed) {
        return;
    }
    s_btn.pressed = pressed;
    if (pressed) {
        bool continues = s_btn.in_sequence && !s_btn.long_fired;
        emit(BUTTON_PRESS_DOWN);
        s_btn.repeat = continues ? s_btn.repeat + 1 : 1;
        s_btn.in_sequence = true;
        s_btn.long_fired = false;
        s_btn.press_ms = now_ms;
        if (s_btn.repeat >= 2) {
            emit(BUTTON_PRESS_REPEAT);
        }
    } else {
        s_btn.release_ms = now_ms;
        emit(BUTTON_PRESS_UP);
    }
}

void sim_button_tick(uint32_t now_ms)
{
    if (s_btn.pressed && !s_btn.long_fired && now_ms - s_btn.press_ms >= BSP_BUTTON_LONG_MS) {
        s_btn.long_fired = true;
        emit(BUTTON_LONG_PRESS_START);
    }
    if (!s_btn.pressed && s_btn.in_sequence &&
        (s_btn.long_fired || now_ms - s_btn.release_ms > BSP_BUTTON_SHORT_MS)) {
        s_btn.in_sequence = false;
        s_btn.long_fired = false;
        s_btn.repeat = 0;
        emit(BUTTON_PRESS_END);
    }
}

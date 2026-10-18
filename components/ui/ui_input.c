#include <inttypes.h>
#include "ui_input.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_log.h"

static const char *TAG = "ui_input";

/* Queue entries: a key to tap, or the start/end of an ENTER hold, or a wake request. */
#define EV_HOLD_DOWN    0x1000
#define EV_HOLD_UP      0x1001
#define EV_WAKE         0x1002

static QueueHandle_t s_events;
static bool s_long_press;
static bool s_waking;
static bool s_in_sequence;

/* Button callbacks run in the button component's timer context: only queue events, never touch LVGL. */
static void queue_event(uint32_t ev)
{
    if (xQueueSend(s_events, &ev, 0) != pdTRUE) {
        ESP_LOGW(TAG, "event queue full, dropped %" PRIu32, ev);
    }
}

/* PRESS_DOWN fires before the component updates its repeat count, so track sequences here. */
static void press_down_cb(void *btn, void *usr_data)
{
    s_long_press = false;
    if (!s_in_sequence) {
        s_in_sequence = true;
        s_waking = !ui_screen_is_on();
    }
}

static void press_end_cb(void *btn, void *usr_data)
{
    s_in_sequence = false;
}

/* A click acts on release without waiting for the double-click window. */
static void press_up_cb(void *btn, void *usr_data)
{
    if (s_long_press) {
        if (!s_waking) {
            queue_event(EV_HOLD_UP);
        }
        return;
    }
    if (iot_button_get_repeat(btn) == 1) {
        queue_event(s_waking ? EV_WAKE : LV_KEY_NEXT);
    }
}

/* The first click of a double click already moved focus forward: undo it, then go back. */
static void press_repeat_cb(void *btn, void *usr_data)
{
    if (!s_waking && iot_button_get_repeat(btn) == 2) {
        queue_event(LV_KEY_PREV);
        queue_event(LV_KEY_ESC);
    }
}

static void long_press_cb(void *btn, void *usr_data)
{
    s_long_press = true;
    queue_event(s_waking ? EV_WAKE : EV_HOLD_DOWN);
}

/*
 * LVGL polls this every input period. A tapped key is reported as a press followed by a release
 * in the same poll (continue_reading); a hold keeps LV_KEY_ENTER pressed across polls until the
 * button is released, which is what gives widgets PRESSING events.
 */
static void read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    static uint32_t key;
    static bool tap_pending;
    static bool holding;

    if (tap_pending) {
        tap_pending = false;
        data->key = key;
        data->state = LV_INDEV_STATE_RELEASED;
        data->continue_reading = uxQueueMessagesWaiting(s_events) > 0;
        return;
    }

    uint32_t ev;
    while (xQueueReceive(s_events, &ev, 0) == pdTRUE) {
        switch (ev) {
        case EV_WAKE:
            ui_screen_wake();
            continue;
        case EV_HOLD_DOWN:
            holding = true;
            key = LV_KEY_ENTER;
            break;
        case EV_HOLD_UP:
            holding = false;
            data->key = LV_KEY_ENTER;
            data->state = LV_INDEV_STATE_RELEASED;
            data->continue_reading = uxQueueMessagesWaiting(s_events) > 0;
            return;
        default:
            if (holding) {
                continue;
            }
            key = ev;
            tap_pending = true;
            data->continue_reading = true;
            break;
        }
        data->key = key;
        data->state = LV_INDEV_STATE_PRESSED;
        return;
    }
    data->key = key;
    data->state = holding ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

lv_indev_t *ui_input_create(button_handle_t btn)
{
    s_events = xQueueCreate(8, sizeof(uint32_t));
    if (!s_events) {
        return NULL;
    }
    iot_button_register_cb(btn, BUTTON_PRESS_DOWN, NULL, press_down_cb, NULL);
    iot_button_register_cb(btn, BUTTON_PRESS_UP, NULL, press_up_cb, NULL);
    iot_button_register_cb(btn, BUTTON_PRESS_REPEAT, NULL, press_repeat_cb, NULL);
    iot_button_register_cb(btn, BUTTON_LONG_PRESS_START, NULL, long_press_cb, NULL);
    iot_button_register_cb(btn, BUTTON_PRESS_END, NULL, press_end_cb, NULL);

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(indev, read_cb);
    return indev;
}

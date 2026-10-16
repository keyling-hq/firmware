#include <inttypes.h>
#include "ui_input.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_log.h"

static const char *TAG = "ui_input";

static QueueHandle_t s_keys;
static bool s_long_press;
static bool s_waking;
static bool s_in_sequence;

/* Button callbacks run in the button component's timer context: only queue keys, never touch LVGL. */
static void queue_key(uint32_t key)
{
    if (xQueueSend(s_keys, &key, 0) != pdTRUE) {
        ESP_LOGW(TAG, "key queue full, dropped key %" PRIu32, key);
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
    if (s_long_press || iot_button_get_repeat(btn) != 1) {
        return;
    }
    queue_key(s_waking ? UI_KEY_WAKE : LV_KEY_NEXT);
}

/* The first click of a double click already moved focus forward: undo it, then go back. */
static void press_repeat_cb(void *btn, void *usr_data)
{
    if (!s_waking && iot_button_get_repeat(btn) == 2) {
        queue_key(LV_KEY_PREV);
        queue_key(LV_KEY_ESC);
    }
}

static void long_press_cb(void *btn, void *usr_data)
{
    s_long_press = true;
    queue_key(s_waking ? UI_KEY_WAKE : LV_KEY_ENTER);
}

/*
 * LVGL polls this every input period. A queued key is reported as a press followed by a release;
 * continue_reading makes LVGL call again immediately, so the whole backlog is consumed in one poll.
 */
static void read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    static uint32_t key;
    static bool pressed;

    if (pressed) {
        pressed = false;
        data->key = key;
        data->state = LV_INDEV_STATE_RELEASED;
        data->continue_reading = uxQueueMessagesWaiting(s_keys) > 0;
        return;
    }

    uint32_t next;
    while (xQueueReceive(s_keys, &next, 0) == pdTRUE) {
        if (next == UI_KEY_WAKE) {
            ui_screen_wake();
            continue;
        }
        key = next;
        pressed = true;
        data->key = key;
        data->state = LV_INDEV_STATE_PRESSED;
        data->continue_reading = true;
        return;
    }
    data->key = key;
    data->state = LV_INDEV_STATE_RELEASED;
}

lv_indev_t *ui_input_create(button_handle_t btn)
{
    s_keys = xQueueCreate(8, sizeof(uint32_t));
    if (!s_keys) {
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

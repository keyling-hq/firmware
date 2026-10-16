#include <inttypes.h>
#include "ui_input.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_log.h"

static const char *TAG = "ui_input";

static QueueHandle_t s_keys;

/* Runs in the button component's timer context: only queue the key, never touch LVGL here. */
static void button_cb(void *btn, void *usr_data)
{
    uint32_t key = (uint32_t)(uintptr_t)usr_data;
    if (xQueueSend(s_keys, &key, 0) != pdTRUE) {
        ESP_LOGW(TAG, "key queue full, dropped key %" PRIu32, key);
    }
}

/*
 * LVGL polls this every input period. A queued gesture is reported as a press followed by a
 * release of its virtual key; continue_reading makes LVGL call again immediately, so a whole
 * gesture (and any backlog) is consumed in one poll instead of one state per period.
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
    if (xQueueReceive(s_keys, &key, 0) == pdTRUE) {
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
    iot_button_register_cb(btn, BUTTON_SINGLE_CLICK, NULL, button_cb, (void *)(uintptr_t)LV_KEY_NEXT);
    iot_button_register_cb(btn, BUTTON_LONG_PRESS_START, NULL, button_cb, (void *)(uintptr_t)LV_KEY_ENTER);
    iot_button_register_cb(btn, BUTTON_DOUBLE_CLICK, NULL, button_cb, (void *)(uintptr_t)LV_KEY_ESC);

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(indev, read_cb);
    return indev;
}

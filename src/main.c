#include "bsp/tdongle.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "ui.h"
#include "usb_hid.h"

static const char *TAG = "main";

#define HID_KEY_A       0x04
#define HID_KEY_SPACE   0x2C

static QueueHandle_t s_actions;

static void nvs_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

/* Lowercase letters and space only: their keycodes are the same on every layout we target. */
static void type_ascii(const char *text)
{
    for (const char *c = text; *c; c++) {
        uint8_t keys[USB_HID_KEYBOARD_KEYS] = { 0 };
        if (*c >= 'a' && *c <= 'z') {
            keys[0] = HID_KEY_A + (*c - 'a');
        } else if (*c == ' ') {
            keys[0] = HID_KEY_SPACE;
        } else {
            continue;
        }
        if (usb_hid_keyboard_report(0, keys) != ESP_OK || usb_hid_keyboard_report(0, NULL) != ESP_OK) {
            ESP_LOGW(TAG, "typing aborted");
            return;
        }
    }
}

static void action_cb(ui_action_t action)
{
    xQueueSend(s_actions, &action, 0);
}

static void action_task(void *arg)
{
    ui_action_t action;
    while (xQueueReceive(s_actions, &action, portMAX_DELAY) == pdTRUE) {
        switch (action) {
        case UI_ACTION_TYPE_TEST:
            type_ascii("hello from the dongle");
            break;
        default:
            break;
        }
    }
}

void app_main(void)
{
    nvs_init();
    ESP_ERROR_CHECK(usb_hid_start());

    lv_display_t *disp = NULL;
    ESP_ERROR_CHECK(bsp_display_start(&disp));

    button_handle_t btn = NULL;
    ESP_ERROR_CHECK(bsp_button_create(&btn));

    s_actions = xQueueCreate(4, sizeof(ui_action_t));
    assert(s_actions);
    xTaskCreate(action_task, "action", 3072, NULL, 5, NULL);

    ESP_ERROR_CHECK(ui_start(btn, action_cb));

    ESP_LOGI(TAG, "started");
}

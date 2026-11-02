/* Stand-ins for the firmware services the UI calls. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "freertos/queue.h"
#include "bsp/tdongle.h"
#include "fw_info.h"
#include "keystore.h"
#include "settings.h"
#include "status_led.h"
#include "ui.h"
#include "usb_hid.h"
#include "sim.h"

const char *esp_err_to_name(esp_err_t code)
{
    return code == ESP_OK ? "ESP_OK" : "ESP_ERR";
}

bool lvgl_port_lock(uint32_t timeout_ms)
{
    (void)timeout_ms;
    lv_lock();
    return true;
}

void lvgl_port_unlock(void)
{
    lv_unlock();
}

struct sim_queue {
    UBaseType_t length, item_size, head, count;
    uint8_t items[];
};

QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t item_size)
{
    QueueHandle_t q = calloc(1, sizeof(*q) + length * item_size);
    if (q) {
        q->length = length;
        q->item_size = item_size;
    }
    return q;
}

BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t wait)
{
    (void)wait;
    if (q->count == q->length) {
        return pdFALSE;
    }
    memcpy(&q->items[((q->head + q->count) % q->length) * q->item_size], item, q->item_size);
    q->count++;
    return pdTRUE;
}

BaseType_t xQueueReceive(QueueHandle_t q, void *item, TickType_t wait)
{
    (void)wait;
    if (q->count == 0) {
        return pdFALSE;
    }
    memcpy(item, &q->items[q->head * q->item_size], q->item_size);
    q->head = (q->head + 1) % q->length;
    q->count--;
    return pdTRUE;
}

UBaseType_t uxQueueMessagesWaiting(QueueHandle_t q)
{
    return q->count;
}

const settings_speed_preset_t settings_speed_presets[] = {
    { "Fast", 2, 2 },
    { "Normal", 5, 5 },
    { "Slow", 20, 20 },
};

const size_t settings_speed_preset_count = sizeof(settings_speed_presets) / sizeof(settings_speed_presets[0]);

static settings_t s_settings = {
    .layout = "en-US",
    .key_press_ms = 5,
    .key_release_ms = 5,
    .brightness = 80,
    .screen_timeout_s = 15,
    .ble_name = "KeylingFE92",
};

esp_err_t settings_init(void)
{
    return ESP_OK;
}

void settings_get(settings_t *out)
{
    *out = s_settings;
}

esp_err_t settings_set(const settings_t *in)
{
    s_settings = *in;
    return ESP_OK;
}

void keystore_status(keystore_status_t *status)
{
    *status = (keystore_status_t){ .root = KEYSTORE_ROOT_DEV };
}

bool keystore_is_secure(void)
{
    return false;
}

const char *fw_info_version(void)
{
    return "v0.2.0-sim";
}

bool usb_hid_disk_ejected(void)
{
    return false;
}

esp_err_t usb_hid_mouse_report(uint8_t buttons, int8_t x, int8_t y, int8_t wheel, int8_t pan)
{
    return ESP_OK;
}

static int s_brightness = 80;

esp_err_t bsp_display_brightness_set(int percent)
{
    s_brightness = percent;
    return ESP_OK;
}

int sim_brightness(void)
{
    return s_brightness;
}

/* The status LED's base pattern, chosen from the Bluetooth state as app.c does. */
static status_led_pattern_t s_led = STATUS_LED_OFF;
static uint32_t s_led_start;

void sim_led_for_ble(ui_ble_state_t state)
{
    status_led_pattern_t pattern = state == UI_BLE_CONNECTED   ? STATUS_LED_CONNECTED
                                   : state == UI_BLE_PAIRING   ? STATUS_LED_PAIRING
                                   : state == UI_BLE_UNPAIRED  ? STATUS_LED_ADVERTISING
                                   : state == UI_BLE_SECURING  ? s_led
                                                               : STATUS_LED_OFF;
    if (pattern != s_led) {
        s_led = pattern;
        s_led_start = lv_tick_get();
    }
}

void status_led_base(status_led_pattern_t *pattern, uint32_t *elapsed_ms)
{
    *pattern = s_led;
    *elapsed_ms = lv_tick_elaps(s_led_start);
}

static bool s_flipped;

esp_err_t bsp_display_flip(bool flipped)
{
    s_flipped = flipped;
    printf("screen %s\n", flipped ? "turned by 180 degrees" : "upright");
    return ESP_OK;
}

bool sim_flipped(void)
{
    return s_flipped;
}

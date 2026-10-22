#include "usb_hid.h"
#include "esp_log.h"

static const char *TAG = "usb_hid";

esp_err_t usb_hid_start(usb_hid_mount_cb_t cb, void *ctx)
{
    ESP_LOGI(TAG, "TinyUSB disabled, HID reports are logged");
    if (cb) {
        cb(true, ctx);
    }
    return ESP_OK;
}

bool usb_hid_is_mounted(void)
{
    return true;
}

uint8_t usb_hid_keyboard_leds(void)
{
    return 0;
}

esp_err_t usb_hid_keyboard_report(uint8_t modifiers, const uint8_t keycodes[USB_HID_KEYBOARD_KEYS])
{
    if (keycodes) {
        ESP_LOGI(TAG, "kbd mod=%02x keys=%02x %02x %02x %02x %02x %02x", modifiers,
                 keycodes[0], keycodes[1], keycodes[2], keycodes[3], keycodes[4], keycodes[5]);
    } else {
        ESP_LOGI(TAG, "kbd mod=%02x keys=--", modifiers);
    }
    return ESP_OK;
}

esp_err_t usb_hid_mouse_report(uint8_t buttons, int8_t x, int8_t y, int8_t wheel, int8_t pan)
{
    ESP_LOGI(TAG, "mouse btn=%02x x=%d y=%d wheel=%d pan=%d", buttons, x, y, wheel, pan);
    return ESP_OK;
}

esp_err_t usb_hid_consumer_report(uint16_t usage)
{
    ESP_LOGI(TAG, "consumer usage=%04x", usage);
    return ESP_OK;
}

void usb_hid_fido_set_rx_cb(usb_hid_fido_rx_cb_t cb, void *ctx)
{
}

bool usb_hid_fido_ready(void)
{
    return false;
}

esp_err_t usb_hid_fido_send(const uint8_t report[USB_HID_FIDO_REPORT_SIZE])
{
    return ESP_ERR_NOT_SUPPORTED;
}

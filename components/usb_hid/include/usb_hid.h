#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define USB_HID_KEYBOARD_KEYS   6

esp_err_t usb_hid_start(void);

/** True once the host has configured the device. */
bool usb_hid_is_mounted(void);

/** Last keyboard LED state from the host (bit 0 num lock, 1 caps lock, 2 scroll lock). */
uint8_t usb_hid_keyboard_leds(void);

/** Blocks until the HID endpoint is free. keycodes may be NULL for an all-released report. */
esp_err_t usb_hid_keyboard_report(uint8_t modifiers, const uint8_t keycodes[USB_HID_KEYBOARD_KEYS]);

esp_err_t usb_hid_mouse_report(uint8_t buttons, int8_t x, int8_t y, int8_t wheel, int8_t pan);

/** usage is a Consumer page usage ID; 0 releases. */
esp_err_t usb_hid_consumer_report(uint16_t usage);

#ifdef __cplusplus
}
#endif

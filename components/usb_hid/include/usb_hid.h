#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define USB_HID_KEYBOARD_KEYS   6

/** Called from the USB task when the host configures or drops the device. */
typedef void (*usb_hid_mount_cb_t)(bool mounted, void *ctx);

esp_err_t usb_hid_start(usb_hid_mount_cb_t cb, void *ctx);

/** True once the host has configured the device. */
bool usb_hid_is_mounted(void);

/** Last keyboard LED state from the host (bit 0 num lock, 1 caps lock, 2 scroll lock). */
uint8_t usb_hid_keyboard_leds(void);

/** Blocks until the HID endpoint is free. keycodes may be NULL for an all-released report. */
esp_err_t usb_hid_keyboard_report(uint8_t modifiers, const uint8_t keycodes[USB_HID_KEYBOARD_KEYS]);

esp_err_t usb_hid_mouse_report(uint8_t buttons, int8_t x, int8_t y, int8_t wheel, int8_t pan);

/** usage is a Consumer page usage ID; 0 releases. */
esp_err_t usb_hid_consumer_report(uint16_t usage);

/* FIDO (CTAPHID) interface: 64-byte reports in both directions. */
#define USB_HID_FIDO_REPORT_SIZE    64

/** Called from the USB task with each report the host sends to the FIDO interface. */
typedef void (*usb_hid_fido_rx_cb_t)(const uint8_t report[USB_HID_FIDO_REPORT_SIZE], void *ctx);

void usb_hid_fido_set_rx_cb(usb_hid_fido_rx_cb_t cb, void *ctx);

/** True when the FIDO IN endpoint can take a report. */
bool usb_hid_fido_ready(void);

/** Non-blocking: fails if the endpoint is busy or the device isn't mounted. */
esp_err_t usb_hid_fido_send(const uint8_t report[USB_HID_FIDO_REPORT_SIZE]);

#ifdef __cplusplus
}
#endif

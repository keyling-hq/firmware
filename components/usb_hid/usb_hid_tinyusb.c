#include <stdio.h>
#include <string.h>
#include "usb_hid.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/usb_serial_jtag_reg.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_cdc_acm.h"
#include "tinyusb_console.h"

static const char *TAG = "usb_hid";

#define USB_PID                 0x4F01
#define HID_EP_SIZE             16
#define HID_READY_TIMEOUT_MS    100
#define DOWNLOAD_TOUCH_BAUD     1200

#define FIDO_POLL_MS            5

enum {
    ITF_NUM_CDC = 0,
    ITF_NUM_CDC_DATA,
    ITF_NUM_HID,
    ITF_NUM_FIDO,
    ITF_NUM_TOTAL,
};

/* TinyUSB numbers HID instances in descriptor order. */
enum {
    HID_INSTANCE_KEYBOARD = 0,
    HID_INSTANCE_FIDO,
};

#define EPNUM_CDC_NOTIF 0x81
#define EPNUM_CDC_OUT   0x02
#define EPNUM_CDC_IN    0x82
#define EPNUM_HID       0x83
#define EPNUM_FIDO_OUT  0x04
#define EPNUM_FIDO_IN   0x84

enum {
    STRID_LANGID = 0,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL,
    STRID_CDC,
    STRID_HID,
    STRID_FIDO,
    STRID_COUNT,
};

enum {
    REPORT_ID_KEYBOARD = 1,
    REPORT_ID_MOUSE,
    REPORT_ID_CONSUMER,
};

static const uint8_t s_hid_report_desc[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(REPORT_ID_KEYBOARD)),
    TUD_HID_REPORT_DESC_MOUSE(HID_REPORT_ID(REPORT_ID_MOUSE)),
    TUD_HID_REPORT_DESC_CONSUMER(HID_REPORT_ID(REPORT_ID_CONSUMER)),
};

static const uint8_t s_fido_report_desc[] = {
    TUD_HID_REPORT_DESC_FIDO_U2F(USB_HID_FIDO_REPORT_SIZE),
};

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_HID_DESC_LEN + TUD_HID_INOUT_DESC_LEN)

static const uint8_t s_config_desc[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, STRID_CDC, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),
    TUD_HID_DESCRIPTOR(ITF_NUM_HID, STRID_HID, HID_ITF_PROTOCOL_NONE, sizeof(s_hid_report_desc), EPNUM_HID, HID_EP_SIZE, 1),
    TUD_HID_INOUT_DESCRIPTOR(ITF_NUM_FIDO, STRID_FIDO, HID_ITF_PROTOCOL_NONE, sizeof(s_fido_report_desc),
                             EPNUM_FIDO_OUT, EPNUM_FIDO_IN, USB_HID_FIDO_REPORT_SIZE, FIDO_POLL_MS),
};

/* IAD class codes are required for a composite device that contains CDC. */
static const tusb_desc_device_t s_device_desc = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = TINYUSB_ESPRESSIF_VID,
    .idProduct = USB_PID,
    .bcdDevice = 0x0100,
    .iManufacturer = STRID_MANUFACTURER,
    .iProduct = STRID_PRODUCT,
    .iSerialNumber = STRID_SERIAL,
    .bNumConfigurations = 1,
};

static char s_serial[13];

static const char *s_strings[STRID_COUNT] = {
    [STRID_LANGID] = "\x09\x04",
    [STRID_MANUFACTURER] = "Keyling",
    [STRID_PRODUCT] = "Keyling",
    [STRID_SERIAL] = s_serial,
    [STRID_CDC] = "Console",
    [STRID_HID] = "Keyboard",
    [STRID_FIDO] = "Security key",
};

static SemaphoreHandle_t s_lock;
static volatile uint8_t s_leds;
static usb_hid_mount_cb_t s_mount_cb;
static void *s_mount_ctx;
static usb_hid_fido_rx_cb_t s_fido_rx_cb;
static void *s_fido_rx_ctx;

static void usb_event_cb(tinyusb_event_t *event, void *arg)
{
    bool mounted = event->id == TINYUSB_EVENT_ATTACHED;
    if (!mounted) {
        s_leds = 0;
    }
    if (s_mount_cb) {
        s_mount_cb(mounted, s_mount_ctx);
    }
}

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    return instance == HID_INSTANCE_FIDO ? s_fido_report_desc : s_hid_report_desc;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen)
{
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize)
{
    if (instance == HID_INSTANCE_FIDO) {
        if (bufsize == USB_HID_FIDO_REPORT_SIZE && s_fido_rx_cb) {
            s_fido_rx_cb(buffer, s_fido_rx_ctx);
        }
        return;
    }
    if (report_id == REPORT_ID_KEYBOARD && report_type == HID_REPORT_TYPE_OUTPUT && bufsize >= 1) {
        s_leds = buffer[0];
    }
}

/*
 * Hands the PHY back to USB-Serial-JTAG and reboots into the ROM download mode, so esptool
 * sees the standard serial/JTAG port and can reset back into the app after flashing.
 */
static void reboot_to_download_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(100));
    tinyusb_driver_uninstall();

    CLEAR_PERI_REG_MASK(RTC_CNTL_USB_CONF_REG,
                        RTC_CNTL_SW_HW_USB_PHY_SEL | RTC_CNTL_SW_USB_PHY_SEL | RTC_CNTL_USB_PAD_ENABLE);
    CLEAR_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_PHY_SEL);
    SET_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_USB_PAD_ENABLE | USB_SERIAL_JTAG_DP_PULLUP);

    REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
    esp_restart();
}

static void cdc_line_coding_cb(int itf, cdcacm_event_t *event)
{
    if (event->line_coding_changed_data.p_line_coding->bit_rate == DOWNLOAD_TOUCH_BAUD) {
        xTaskCreate(reboot_to_download_task, "usb_dl_reboot", 3072, NULL, 10, NULL);
    }
}

esp_err_t usb_hid_start(usb_hid_mount_cb_t cb, void *ctx)
{
    s_mount_cb = cb;
    s_mount_ctx = ctx;
    s_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_lock, ESP_ERR_NO_MEM, TAG, "mutex");

    uint8_t mac[6];
    ESP_RETURN_ON_ERROR(esp_efuse_mac_get_default(mac), TAG, "mac");
    snprintf(s_serial, sizeof(s_serial), "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    tinyusb_config_t cfg = TINYUSB_DEFAULT_CONFIG(usb_event_cb);
    cfg.descriptor.device = &s_device_desc;
    cfg.descriptor.full_speed_config = s_config_desc;
    cfg.descriptor.string = s_strings;
    cfg.descriptor.string_count = STRID_COUNT;
    ESP_RETURN_ON_ERROR(tinyusb_driver_install(&cfg), TAG, "driver install");

    const tinyusb_config_cdcacm_t cdc_cfg = {
        .cdc_port = TINYUSB_CDC_ACM_0,
        .callback_line_coding_changed = cdc_line_coding_cb,
    };
    ESP_RETURN_ON_ERROR(tinyusb_cdcacm_init(&cdc_cfg), TAG, "cdc init");
    ESP_RETURN_ON_ERROR(tinyusb_console_init(TINYUSB_CDC_ACM_0), TAG, "console");

    ESP_LOGI(TAG, "started");
    return ESP_OK;
}

bool usb_hid_is_mounted(void)
{
    return tud_mounted();
}

uint8_t usb_hid_keyboard_leds(void)
{
    return s_leds;
}

void usb_hid_fido_set_rx_cb(usb_hid_fido_rx_cb_t cb, void *ctx)
{
    s_fido_rx_ctx = ctx;
    s_fido_rx_cb = cb;
}

bool usb_hid_fido_ready(void)
{
    return tud_mounted() && tud_hid_n_ready(HID_INSTANCE_FIDO);
}

esp_err_t usb_hid_fido_send(const uint8_t report[USB_HID_FIDO_REPORT_SIZE])
{
    if (!usb_hid_fido_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    return tud_hid_n_report(HID_INSTANCE_FIDO, 0, report, USB_HID_FIDO_REPORT_SIZE) ? ESP_OK : ESP_FAIL;
}

static esp_err_t wait_ready(void)
{
    for (int i = 0; i < HID_READY_TIMEOUT_MS; i++) {
        if (!tud_mounted()) {
            return ESP_ERR_INVALID_STATE;
        }
        if (tud_hid_n_ready(HID_INSTANCE_KEYBOARD)) {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return ESP_ERR_TIMEOUT;
}

static esp_err_t send_report(uint8_t report_id, const void *report, uint16_t len)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t ret = wait_ready();
    if (ret == ESP_OK && !tud_hid_n_report(HID_INSTANCE_KEYBOARD, report_id, report, len)) {
        ret = ESP_FAIL;
    }
    xSemaphoreGive(s_lock);
    return ret;
}

esp_err_t usb_hid_keyboard_report(uint8_t modifiers, const uint8_t keycodes[USB_HID_KEYBOARD_KEYS])
{
    hid_keyboard_report_t report = { .modifier = modifiers };
    if (keycodes) {
        memcpy(report.keycode, keycodes, USB_HID_KEYBOARD_KEYS);
    }
    return send_report(REPORT_ID_KEYBOARD, &report, sizeof(report));
}

esp_err_t usb_hid_mouse_report(uint8_t buttons, int8_t x, int8_t y, int8_t wheel, int8_t pan)
{
    const hid_mouse_report_t report = {
        .buttons = buttons,
        .x = x,
        .y = y,
        .wheel = wheel,
        .pan = pan,
    };
    return send_report(REPORT_ID_MOUSE, &report, sizeof(report));
}

esp_err_t usb_hid_consumer_report(uint16_t usage)
{
    return send_report(REPORT_ID_CONSUMER, &usage, sizeof(usage));
}

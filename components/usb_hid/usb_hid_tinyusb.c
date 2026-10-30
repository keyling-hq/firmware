#include <stdio.h>
#include <string.h>
#include "usb_hid.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "device/usbd_pvt.h"

static const char *TAG = "usb_hid";

#define USB_PID                 0x4F01
/* Changes whenever the interface set does, so hosts don't reuse cached driver bindings. */
#define USB_BCD_DEVICE          0x0201
#define HID_EP_SIZE             16
#define HID_READY_TIMEOUT_MS    100

#define FIDO_POLL_MS            5

enum {
    ITF_NUM_HID = 0,
    ITF_NUM_FIDO,
    ITF_NUM_CCID,
    ITF_NUM_TOTAL,
};

/* TinyUSB numbers HID instances in descriptor order. */
enum {
    HID_INSTANCE_KEYBOARD = 0,
    HID_INSTANCE_FIDO,
};

#define EPNUM_HID       0x81
#define EPNUM_FIDO_OUT  0x02
#define EPNUM_FIDO_IN   0x82
#define EPNUM_CCID_OUT  0x03
#define EPNUM_CCID_IN   0x83
#define EPNUM_CCID_INT  0x84

enum {
    STRID_LANGID = 0,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL,
    STRID_HID,
    STRID_FIDO,
    STRID_CCID,
    STRID_COUNT,
};

/*
 * USB CCID 1.1 interface with one slot, T=1 and extended APDUs. Byte layout as in canokey-core's
 * descriptor. The interrupt endpoint tells the host when the card is removed or inserted: hosts
 * don't poll for it.
 */
#define CCID_INT_EP_SIZE    8
#define CCID_INT_INTERVAL   16
#define TUD_CCID_DESC_LEN   (9 + 54 + 7 + 7 + 7)
#define TUD_CCID_DESCRIPTOR(itf, stridx, epout, epin, epint, epsize, max_message, max_ifsd)                       \
    9, TUSB_DESC_INTERFACE, itf, 0, 3, TUSB_CLASS_SMART_CARD, 0, 0, stridx,                                       \
    54, 0x21, U16_TO_U8S_LE(0x0110), 0, 0x07, U32_TO_U8S_LE(0x00000002), U32_TO_U8S_LE(4000),                     \
    U32_TO_U8S_LE(4000), 0, U32_TO_U8S_LE(307200), U32_TO_U8S_LE(307200), 0, U32_TO_U8S_LE(max_ifsd),             \
    U32_TO_U8S_LE(0), U32_TO_U8S_LE(0), U32_TO_U8S_LE(0x000400FE), U32_TO_U8S_LE(max_message), 0xFF, 0xFF,       \
    U16_TO_U8S_LE(0), 0, 1,                                                                                       \
    7, TUSB_DESC_ENDPOINT, epin, TUSB_XFER_BULK, U16_TO_U8S_LE(epsize), 0,                                        \
    7, TUSB_DESC_ENDPOINT, epout, TUSB_XFER_BULK, U16_TO_U8S_LE(epsize), 0,                                       \
    7, TUSB_DESC_ENDPOINT, epint, TUSB_XFER_INTERRUPT, U16_TO_U8S_LE(CCID_INT_EP_SIZE), CCID_INT_INTERVAL

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

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN + TUD_HID_INOUT_DESC_LEN + TUD_CCID_DESC_LEN)

static const uint8_t s_config_desc[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_HID_DESCRIPTOR(ITF_NUM_HID, STRID_HID, HID_ITF_PROTOCOL_NONE, sizeof(s_hid_report_desc), EPNUM_HID, HID_EP_SIZE, 1),
    TUD_HID_INOUT_DESCRIPTOR(ITF_NUM_FIDO, STRID_FIDO, HID_ITF_PROTOCOL_NONE, sizeof(s_fido_report_desc),
                             EPNUM_FIDO_OUT, EPNUM_FIDO_IN, USB_HID_FIDO_REPORT_SIZE, FIDO_POLL_MS),
    TUD_CCID_DESCRIPTOR(ITF_NUM_CCID, STRID_CCID, EPNUM_CCID_OUT, EPNUM_CCID_IN, EPNUM_CCID_INT, USB_HID_CCID_PACKET_SIZE,
                        USB_HID_CCID_MAX_MESSAGE, USB_HID_CCID_MAX_MESSAGE - 10),
};

static const tusb_desc_device_t s_device_desc = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = TINYUSB_ESPRESSIF_VID,
    .idProduct = USB_PID,
    .bcdDevice = USB_BCD_DEVICE,
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
    [STRID_HID] = "Keyboard",
    [STRID_FIDO] = "Security key",
    [STRID_CCID] = "Smartcard",
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

/* ---- CCID: an application class driver, TinyUSB has none ------------------ */

static uint8_t s_ccid_ep_in;
static uint8_t s_ccid_ep_out;
static uint8_t s_ccid_ep_int;
static CFG_TUSB_MEM_ALIGN uint8_t s_ccid_int_buf[CCID_INT_EP_SIZE];
static CFG_TUSB_MEM_ALIGN uint8_t s_ccid_rx_buf[USB_HID_CCID_PACKET_SIZE];
static usb_hid_ccid_rx_cb_t s_ccid_rx_cb;
static usb_hid_ccid_tx_done_cb_t s_ccid_tx_done_cb;
static void *s_ccid_ctx;

static void ccid_init(void)
{
}

static bool ccid_deinit(void)
{
    return true;
}

static void ccid_reset(uint8_t rhport)
{
    s_ccid_ep_in = 0;
    s_ccid_ep_out = 0;
    s_ccid_ep_int = 0;
}

static uint16_t ccid_open(uint8_t rhport, tusb_desc_interface_t const *itf, uint16_t max_len)
{
    TU_VERIFY(itf->bInterfaceClass == TUSB_CLASS_SMART_CARD, 0);
    uint8_t const *p = (uint8_t const *)itf;
    uint16_t len = 0;
    do {
        len += tu_desc_len(p);
        p = tu_desc_next(p);
    } while (len < max_len && tu_desc_type(p) != TUSB_DESC_ENDPOINT);
    TU_ASSERT(usbd_open_edpt_pair(rhport, p, 2, TUSB_XFER_BULK, &s_ccid_ep_out, &s_ccid_ep_in), 0);
    len += 2 * sizeof(tusb_desc_endpoint_t);
    p = tu_desc_next(tu_desc_next(p));
    if (len < max_len && tu_desc_type(p) == TUSB_DESC_ENDPOINT) {
        tusb_desc_endpoint_t const *ep = (tusb_desc_endpoint_t const *)p;
        TU_ASSERT(ep->bmAttributes.xfer == TUSB_XFER_INTERRUPT && usbd_edpt_open(rhport, ep), 0);
        s_ccid_ep_int = ep->bEndpointAddress;
        len += sizeof(tusb_desc_endpoint_t);
    }
    TU_ASSERT(usbd_edpt_xfer(rhport, s_ccid_ep_out, s_ccid_rx_buf, sizeof(s_ccid_rx_buf), false), 0);
    return len;
}

#define CCID_REQUEST_ABORT  0x01

/* ABORT is acknowledged; the PC_to_RDR_Abort that follows on the bulk pipe is the card's business. */
static bool ccid_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request)
{
    if (request->bmRequestType_bit.type != TUSB_REQ_TYPE_CLASS || request->bRequest != CCID_REQUEST_ABORT) {
        return false;
    }
    if (stage == CONTROL_STAGE_SETUP) {
        return tud_control_status(rhport, request);
    }
    return true;
}

static bool ccid_xfer_cb(uint8_t rhport, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes)
{
    if (ep_addr == s_ccid_ep_out) {
        if (result == XFER_RESULT_SUCCESS && s_ccid_rx_cb) {
            s_ccid_rx_cb(s_ccid_rx_buf, xferred_bytes, s_ccid_ctx);
        }
        return usbd_edpt_xfer(rhport, s_ccid_ep_out, s_ccid_rx_buf, sizeof(s_ccid_rx_buf), false);
    }
    if (ep_addr == s_ccid_ep_in && s_ccid_tx_done_cb) {
        s_ccid_tx_done_cb(s_ccid_ctx);
    }
    return true;
}

static const usbd_class_driver_t s_ccid_driver = {
    .name = "CCID",
    .init = ccid_init,
    .deinit = ccid_deinit,
    .reset = ccid_reset,
    .open = ccid_open,
    .control_xfer_cb = ccid_control_xfer_cb,
    .xfer_cb = ccid_xfer_cb,
};

usbd_class_driver_t const *usbd_app_driver_get_cb(uint8_t *driver_count)
{
    *driver_count = 1;
    return &s_ccid_driver;
}

void usb_hid_ccid_set_callbacks(usb_hid_ccid_rx_cb_t rx, usb_hid_ccid_tx_done_cb_t tx_done, void *ctx)
{
    s_ccid_ctx = ctx;
    s_ccid_rx_cb = rx;
    s_ccid_tx_done_cb = tx_done;
}

esp_err_t usb_hid_ccid_send(const uint8_t *data, size_t len)
{
    if (!tud_mounted() || !s_ccid_ep_in || len > UINT16_MAX) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!usbd_edpt_claim(0, s_ccid_ep_in)) {
        return ESP_ERR_INVALID_STATE;
    }
    bool ok = usbd_edpt_xfer(0, s_ccid_ep_in, (uint8_t *)data, (uint16_t)len, false);
    if (!ok) {
        usbd_edpt_release(0, s_ccid_ep_in);
    }
    return ok ? ESP_OK : ESP_FAIL;
}

esp_err_t usb_hid_ccid_notify(const uint8_t *data, size_t len)
{
    if (!tud_mounted() || !s_ccid_ep_int || len > sizeof(s_ccid_int_buf)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!usbd_edpt_claim(0, s_ccid_ep_int)) {
        return ESP_ERR_NOT_FINISHED;
    }
    memcpy(s_ccid_int_buf, data, len);
    bool ok = usbd_edpt_xfer(0, s_ccid_ep_int, s_ccid_int_buf, (uint16_t)len, false);
    if (!ok) {
        usbd_edpt_release(0, s_ccid_ep_int);
    }
    return ok ? ESP_OK : ESP_FAIL;
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

/*
 * Replaces canokey-core's usbd_ccid.c (written for its own STM32 USB stack, where it ran in the USB
 * interrupt) on top of TinyUSB, through the usb_hid component. Received packets and finished
 * transfers are queued and handed to canokey's CCID code on the canokey task; only the bulk IN
 * state is shared with the USB task, since canokey waits on it while sending.
 */
#include <string.h>
#include "ccid_glue.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_check.h"
#include "esp_log.h"
#include "usb_hid.h"
#include <ccid.h>
#include <device.h>
#include <usb_device.h>
#include <usbd_ccid.h>

static const char *TAG = "ccid";

/* TinyUSB re-arms the OUT endpoint at once, so a whole command (up to ~50 packets) may queue up. */
#define EVENT_QUEUE_LEN     64
#define IN_IDLE_TIMEOUT_MS  2000

_Static_assert(CCID_MAX_MESSAGE_SIZE <= USB_HID_CCID_MAX_MESSAGE, "USB_HID_CCID_MAX_MESSAGE too small");
_Static_assert(USB_HID_CCID_PACKET_SIZE == 64, "canokey's CCID code assumes 64-byte packets");

typedef enum {
    EVENT_RX,
    EVENT_TX_DONE,
} event_type_t;

typedef struct {
    uint8_t type;
    uint8_t len;
    uint8_t time_extension;
    uint8_t data[USB_HID_CCID_PACKET_SIZE];
} event_t;

static QueueHandle_t s_events;
static volatile uint8_t s_in_state = CCID_STATE_IDLE;
/* The USB DMA needs word-aligned buffers; canokey's response structs are packed. */
static uint8_t s_tx_buf[CCID_CMD_HEADER_SIZE + ABDATA_SIZE] __attribute__((aligned(4)));
static volatile uint32_t s_rx_count;
static volatile uint32_t s_tx_count;
static volatile uint32_t s_dropped;

static void post(const event_t *event)
{
    if (xQueueSend(s_events, event, 0) != pdTRUE) {
        s_dropped++;
    }
}

static void rx_cb(const uint8_t *data, size_t len, void *ctx)
{
    event_t event = { .type = EVENT_RX, .len = (uint8_t)(len < sizeof(event.data) ? len : sizeof(event.data)) };
    memcpy(event.data, data, event.len);
    s_rx_count++;
    post(&event);
}

static void tx_done_cb(void *ctx)
{
    if (s_in_state == CCID_STATE_DATA_IN_WITH_ZLP) {
        s_in_state = CCID_STATE_DATA_IN;
        if (usb_hid_ccid_send(s_tx_buf, 0) == ESP_OK) {
            return;
        }
    }
    event_t event = {
        .type = EVENT_TX_DONE,
        .time_extension = s_in_state == CCID_STATE_DATA_IN_TIME_EXTENSION,
    };
    s_in_state = CCID_STATE_IDLE;
    s_tx_count++;
    post(&event);
}

/* Called by canokey from the canokey task, and for time extensions from the timer task. */
uint8_t CCID_Response_SendData(USBD_HandleTypeDef *pdev, const uint8_t *buf, uint16_t len,
                               uint8_t is_time_extension_request)
{
    if (!usb_hid_is_mounted()) {
        return USBD_OK;
    }
    for (uint32_t waited = 0; s_in_state != CCID_STATE_IDLE; waited++) {
        if (is_time_extension_request) {
            return USBD_OK;
        }
        if (waited >= IN_IDLE_TIMEOUT_MS) {
            return USBD_BUSY;
        }
        device_delay(1);
    }
    if (len > sizeof(s_tx_buf)) {
        return USBD_FAIL;
    }
    memcpy(s_tx_buf, buf, len);
    if (is_time_extension_request) {
        s_in_state = CCID_STATE_DATA_IN_TIME_EXTENSION;
    } else {
        s_in_state = len % USB_HID_CCID_PACKET_SIZE == 0 ? CCID_STATE_DATA_IN_WITH_ZLP : CCID_STATE_DATA_IN;
    }
    if (usb_hid_ccid_send(s_tx_buf, len) != ESP_OK) {
        s_in_state = CCID_STATE_IDLE;
        return USBD_FAIL;
    }
    return USBD_OK;
}

void ccid_glue_poll(void)
{
    event_t event;
    while (s_events && xQueueReceive(s_events, &event, 0) == pdTRUE) {
        if (event.type == EVENT_RX) {
            CCID_OutEvent(event.data, event.len);
        } else {
            CCID_InFinished(event.time_extension);
        }
    }
}

bool ccid_glue_sending(void)
{
    return s_in_state != CCID_STATE_IDLE;
}

void ccid_glue_log_diagnostics(void)
{
    ESP_LOGI(TAG, "packets: %lu received, %lu responses sent, %lu events dropped, %u queued, in state %u",
             (unsigned long)s_rx_count, (unsigned long)s_tx_count, (unsigned long)s_dropped,
             s_events ? (unsigned)uxQueueMessagesWaiting(s_events) : 0, s_in_state);
}

#define RDR_TO_PC_NOTIFYSLOTCHANGE  0x50
#define SLOT0_PRESENT               0x01
#define SLOT0_CHANGED               0x02

esp_err_t ccid_glue_notify_slot(bool present)
{
    const uint8_t msg[] = { RDR_TO_PC_NOTIFYSLOTCHANGE, SLOT0_CHANGED | (present ? SLOT0_PRESENT : 0) };
    return usb_hid_ccid_notify(msg, sizeof(msg));
}

esp_err_t ccid_glue_start(void)
{
    if (s_events) {
        return ESP_OK;
    }
    s_events = xQueueCreate(EVENT_QUEUE_LEN, sizeof(event_t));
    ESP_RETURN_ON_FALSE(s_events, ESP_ERR_NO_MEM, TAG, "queue");
    CCID_Init();
    usb_hid_ccid_set_callbacks(rx_cb, tx_done_cb, NULL);
    return ESP_OK;
}

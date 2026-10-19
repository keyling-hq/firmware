/*
 * Replaces canokey-core's usbd_ctaphid.c (written for its own STM32 USB stack) with the same
 * USBD_CTAPHID_* functions on top of TinyUSB, through the usb_hid component.
 */
#include <string.h>
#include "ctaphid_glue.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_check.h"
#include "esp_log.h"
#include "usb_hid.h"
#include <ctaphid.h>
#include <device.h>
#include <usbd_ctaphid.h>

static const char *TAG = "ctaphid";

/*
 * TinyUSB re-arms the OUT endpoint right after delivering a report, so the host can't be held
 * off: reports are queued here and fed to the framing as fast as it accepts them.
 */
#define RX_QUEUE_LEN        64
#define WAIT_IDLE_MS        100
#define SEND_IDLE_MS        50
#define RX_WAIT_MS          2

/* Referenced by the framing; only the STM32 stack it was written for looks inside. */
USBD_HandleTypeDef usb_device;

static QueueHandle_t s_rx;
static uint8_t s_pending[USB_HID_FIDO_REPORT_SIZE];
static bool s_has_pending;
static bool s_overflow_logged;
static volatile uint32_t s_rx_count;
static volatile uint32_t s_tx_count;
static volatile uint32_t s_tx_busy;

static void rx_cb(const uint8_t report[USB_HID_FIDO_REPORT_SIZE], void *ctx)
{
    s_rx_count++;
    if (xQueueSend(s_rx, report, 0) != pdTRUE && !s_overflow_logged) {
        s_overflow_logged = true;
        ESP_LOGE(TAG, "rx queue full: reports dropped");
    }
}

static uint8_t wait_idle(int max_ms)
{
    for (int i = 0; i < max_ms; i++) {
        if (usb_hid_fido_ready()) {
            return USBD_OK;
        }
        device_delay(1);
    }
    return usb_hid_fido_ready() ? USBD_OK : USBD_BUSY;
}

uint8_t USBD_CTAPHID_SendReport(USBD_HandleTypeDef *pdev, uint8_t *report, uint16_t len)
{
    if (wait_idle(SEND_IDLE_MS) != USBD_OK) {
        s_tx_busy++;
        return USBD_BUSY;
    }
    uint8_t buf[USB_HID_FIDO_REPORT_SIZE] = { 0 };
    memcpy(buf, report, len < sizeof(buf) ? len : sizeof(buf));
    if (usb_hid_fido_send(buf) != ESP_OK) {
        s_tx_busy++;
        return USBD_BUSY;
    }
    s_tx_count++;
    return USBD_OK;
}

void ctaphid_glue_log_diagnostics(void)
{
    ESP_LOGI(TAG, "reports: %lu received, %lu sent, %lu send failures, %u queued, endpoint %s",
             (unsigned long)s_rx_count, (unsigned long)s_tx_count, (unsigned long)s_tx_busy,
             s_rx ? (unsigned)uxQueueMessagesWaiting(s_rx) : 0, usb_hid_fido_ready() ? "ready" : "busy");
}

uint8_t USBD_CTAPHID_IsIdle(void)
{
    return usb_hid_fido_ready() ? USBD_OK : USBD_BUSY;
}

uint8_t USBD_CTAPHID_WaitIdle(void)
{
    return wait_idle(WAIT_IDLE_MS);
}

/*
 * Called by the framing from the canokey task, including from canokey's busy-wait for user
 * presence: blocking briefly when nothing is queued keeps that loop from starving other tasks.
 */
void USBD_CTAPHID_ServiceReceive(void)
{
    for (;;) {
        if (!s_has_pending) {
            if (xQueueReceive(s_rx, s_pending, pdMS_TO_TICKS(RX_WAIT_MS)) != pdTRUE) {
                return;
            }
            s_has_pending = true;
        }
        if (!CTAPHID_RxCanAccept() || !CTAPHID_OutEvent(s_pending)) {
            return;
        }
        s_has_pending = false;
    }
}

esp_err_t ctaphid_glue_start(void)
{
    if (s_rx) {
        return ESP_OK;
    }
    s_rx = xQueueCreate(RX_QUEUE_LEN, USB_HID_FIDO_REPORT_SIZE);
    ESP_RETURN_ON_FALSE(s_rx, ESP_ERR_NO_MEM, TAG, "queue");
    CTAPHID_TxReset();
    CTAPHID_Init(USBD_CTAPHID_SendReport);
    usb_hid_fido_set_rx_cb(rx_cb, NULL);
    return ESP_OK;
}

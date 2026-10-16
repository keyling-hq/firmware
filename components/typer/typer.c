#include <string.h>
#include "typer.h"
#include "freertos/ringbuf.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
#include "usb_hid.h"

static const char *TAG = "typer";

#define QUEUE_BYTES         4096
#define TEXT_CHUNK          256
#define CAPS_LOCK_LED       0x02

typedef enum {
    ITEM_TEXT,
    ITEM_COMBO,
    ITEM_CONSUMER,
} item_type_t;

typedef struct {
    uint8_t type;
    uint8_t modifiers;
    uint16_t usage;
    uint8_t keys[USB_HID_KEYBOARD_KEYS];
} item_header_t;

static RingbufHandle_t s_queue;
static typer_event_cb_t s_cb;
static void *s_cb_ctx;
static const keymap_layout_t *volatile s_layout;
static volatile uint8_t s_press_ms = 5;
static volatile uint8_t s_release_ms = 5;
static volatile bool s_abort;

static struct {
    uint32_t codepoint;
    int remaining;
} s_utf8;

static void emit(typer_event_t event)
{
    if (s_cb) {
        s_cb(event, s_cb_ctx);
    }
}

static void delay_ms(uint8_t ms)
{
    if (ms) {
        vTaskDelay(pdMS_TO_TICKS(ms));
    }
}

static esp_err_t tap(uint8_t modifiers, const uint8_t keys[USB_HID_KEYBOARD_KEYS])
{
    if (modifiers) {
        ESP_RETURN_ON_ERROR(usb_hid_keyboard_report(modifiers, NULL), TAG, "modifiers");
    }
    ESP_RETURN_ON_ERROR(usb_hid_keyboard_report(modifiers, keys), TAG, "press");
    delay_ms(s_press_ms);
    ESP_RETURN_ON_ERROR(usb_hid_keyboard_report(0, NULL), TAG, "release");
    delay_ms(s_release_ms);
    return ESP_OK;
}

static esp_err_t type_codepoint(uint32_t codepoint, size_t *unmapped)
{
    const keymap_layout_t *layout = s_layout;
    keymap_stroke_t strokes[KEYMAP_MAX_STROKES];
    bool caps = usb_hid_keyboard_leds() & CAPS_LOCK_LED;
    size_t count = layout ? keymap_lookup(layout, codepoint, caps, strokes) : 0;
    if (count == 0) {
        if (codepoint != '\r') {
            (*unmapped)++;
        }
        return ESP_OK;
    }
    for (size_t i = 0; i < count && !s_abort; i++) {
        uint8_t keys[USB_HID_KEYBOARD_KEYS] = { strokes[i].keycode };
        ESP_RETURN_ON_ERROR(tap(strokes[i].modifiers, keys), TAG, "tap");
    }
    return ESP_OK;
}

static esp_err_t type_text(const uint8_t *text, size_t len)
{
    size_t unmapped = 0;
    for (size_t i = 0; i < len && !s_abort; i++) {
        uint8_t c = text[i];
        if (s_utf8.remaining && (c & 0xC0) == 0x80) {
            s_utf8.codepoint = (s_utf8.codepoint << 6) | (c & 0x3F);
            if (--s_utf8.remaining) {
                continue;
            }
        } else if (c < 0x80) {
            s_utf8.codepoint = c;
            s_utf8.remaining = 0;
        } else if ((c & 0xE0) == 0xC0) {
            s_utf8 = (typeof(s_utf8)){ c & 0x1F, 1 };
            continue;
        } else if ((c & 0xF0) == 0xE0) {
            s_utf8 = (typeof(s_utf8)){ c & 0x0F, 2 };
            continue;
        } else if ((c & 0xF8) == 0xF0) {
            s_utf8 = (typeof(s_utf8)){ c & 0x07, 3 };
            continue;
        } else {
            s_utf8.remaining = 0;
            unmapped++;
            continue;
        }
        ESP_RETURN_ON_ERROR(type_codepoint(s_utf8.codepoint, &unmapped), TAG, "type");
    }
    /* Never log the characters themselves: this text is often a password. */
    if (unmapped) {
        ESP_LOGW(TAG, "%u characters have no mapping in the current layout", (unsigned)unmapped);
    }
    return ESP_OK;
}

static esp_err_t process(const item_header_t *hdr, const uint8_t *payload, size_t payload_len)
{
    switch (hdr->type) {
    case ITEM_TEXT:
        return type_text(payload, payload_len);
    case ITEM_COMBO:
        return tap(hdr->modifiers, hdr->keys);
    case ITEM_CONSUMER:
        ESP_RETURN_ON_ERROR(usb_hid_consumer_report(hdr->usage), TAG, "consumer press");
        delay_ms(s_press_ms);
        return usb_hid_consumer_report(0);
    default:
        return ESP_OK;
    }
}

static void drain(void)
{
    size_t size;
    void *item;
    while ((item = xRingbufferReceive(s_queue, &size, 0)) != NULL) {
        vRingbufferReturnItem(s_queue, item);
    }
    s_utf8.remaining = 0;
}

static bool queue_empty(void)
{
    UBaseType_t waiting = 0;
    vRingbufferGetInfo(s_queue, NULL, NULL, NULL, NULL, &waiting);
    return waiting == 0;
}

static void typer_task(void *arg)
{
    bool busy = false;
    for (;;) {
        size_t size;
        item_header_t *item = xRingbufferReceive(s_queue, &size, portMAX_DELAY);
        if (!item) {
            continue;
        }
        if (!busy) {
            busy = true;
            emit(TYPER_EVENT_BUSY);
        }

        esp_err_t err = ESP_OK;
        if (!s_abort) {
            err = process(item, (const uint8_t *)(item + 1), size - sizeof(*item));
        }
        vRingbufferReturnItem(s_queue, item);

        if (err != ESP_OK || s_abort) {
            drain();
            usb_hid_keyboard_report(0, NULL);
            usb_hid_consumer_report(0);
            s_abort = false;
            busy = false;
            emit(err != ESP_OK ? TYPER_EVENT_ERROR : TYPER_EVENT_IDLE);
        } else if (queue_empty()) {
            busy = false;
            emit(TYPER_EVENT_IDLE);
        }
    }
}

esp_err_t typer_start(typer_event_cb_t cb, void *ctx)
{
    s_cb = cb;
    s_cb_ctx = ctx;
    s_queue = xRingbufferCreate(QUEUE_BYTES, RINGBUF_TYPE_NOSPLIT);
    ESP_RETURN_ON_FALSE(s_queue, ESP_ERR_NO_MEM, TAG, "queue");
    BaseType_t ok = xTaskCreate(typer_task, "typer", 3072, NULL, 6, NULL);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

void typer_set_layout(const keymap_layout_t *layout)
{
    s_layout = layout;
}

void typer_set_timing(uint8_t key_press_ms, uint8_t key_release_ms)
{
    s_press_ms = key_press_ms;
    s_release_ms = key_release_ms;
}

static esp_err_t enqueue(const item_header_t *hdr, const void *payload, size_t len, TickType_t wait)
{
    void *slot = NULL;
    if (xRingbufferSendAcquire(s_queue, &slot, sizeof(*hdr) + len, wait) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    memcpy(slot, hdr, sizeof(*hdr));
    if (len) {
        memcpy((uint8_t *)slot + sizeof(*hdr), payload, len);
    }
    xRingbufferSendComplete(s_queue, slot);
    return ESP_OK;
}

esp_err_t typer_text(const char *utf8, size_t len, TickType_t wait)
{
    const item_header_t hdr = { .type = ITEM_TEXT };
    while (len) {
        size_t chunk = len < TEXT_CHUNK ? len : TEXT_CHUNK;
        ESP_RETURN_ON_ERROR(enqueue(&hdr, utf8, chunk, wait), TAG, "text");
        utf8 += chunk;
        len -= chunk;
    }
    return ESP_OK;
}

esp_err_t typer_combo(uint8_t modifiers, const uint8_t *keys, size_t key_count, TickType_t wait)
{
    item_header_t hdr = { .type = ITEM_COMBO, .modifiers = modifiers };
    ESP_RETURN_ON_FALSE(key_count <= USB_HID_KEYBOARD_KEYS, ESP_ERR_INVALID_ARG, TAG, "too many keys");
    if (key_count) {
        memcpy(hdr.keys, keys, key_count);
    }
    return enqueue(&hdr, NULL, 0, wait);
}

esp_err_t typer_consumer(uint16_t usage, TickType_t wait)
{
    const item_header_t hdr = { .type = ITEM_CONSUMER, .usage = usage };
    return enqueue(&hdr, NULL, 0, wait);
}

void typer_abort(void)
{
    s_abort = true;
}

size_t typer_free_bytes(void)
{
    return xRingbufferGetCurFreeSize(s_queue);
}

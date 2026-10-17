#pragma once

#include <stddef.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "esp_err.h"
#include "keymap.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TYPER_EVENT_BUSY,       /* started working through queued input */
    TYPER_EVENT_IDLE,       /* queue drained, or aborted */
    TYPER_EVENT_ERROR,      /* a report could not be sent; the queue was dropped */
} typer_event_t;

/** Called from the typer task. */
typedef void (*typer_event_cb_t)(typer_event_t event, void *ctx);

esp_err_t typer_start(typer_event_cb_t cb, void *ctx);

void typer_set_layout(const keymap_layout_t *layout);

void typer_set_timing(uint8_t key_press_ms, uint8_t key_release_ms);

/* Text longer than this is queued as several items, each needing its own room in the queue. */
#define TYPER_TEXT_CHUNK    256

/**
 * Queue UTF-8 text. Text may be split anywhere, including inside a multi-byte sequence.
 * Returns ESP_ERR_TIMEOUT if the queue doesn't have room within wait.
 */
esp_err_t typer_text(const char *utf8, size_t len, TickType_t wait);

/** Queue a chord: modifiers plus up to 6 keys pressed together, then released. */
esp_err_t typer_combo(uint8_t modifiers, const uint8_t *keys, size_t key_count, TickType_t wait);

/** Queue a raw keyboard report, sent as is: the caller releases keys with a later empty report. */
esp_err_t typer_keyboard_report(uint8_t modifiers, const uint8_t *keys, size_t key_count, TickType_t wait);

/** Queue a consumer control usage press and release. */
esp_err_t typer_consumer(uint16_t usage, TickType_t wait);

/** Drops everything queued so far, stops typing and releases every key. Input queued later is kept. */
void typer_abort(void);

size_t typer_free_bytes(void);

#ifdef __cplusplus
}
#endif

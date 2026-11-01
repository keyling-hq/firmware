#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Patterns describe connection state and requests for the user's attention. Nothing here may
 * ever reflect keystrokes or typing activity: LED timing would leak what is being typed.
 */
typedef enum {
    STATUS_LED_OFF,
    STATUS_LED_ADVERTISING,     /* slow blue pulse */
    STATUS_LED_PAIRING,         /* fast yellow blink */
    STATUS_LED_CONNECTED,       /* green for 0.4 s, fading out by 1 s, then off */
    STATUS_LED_ERROR,           /* three red blinks, then off */
} status_led_pattern_t;

/* Shown instead of the base pattern while set; each context has its own color and rhythm. */
typedef enum {
    STATUS_LED_ATTENTION_NONE,
    STATUS_LED_ATTENTION_FIDO,  /* magenta double blink: a security key request waits for approval */
} status_led_attention_t;

esp_err_t status_led_start(void);

/** Sets the base pattern; it restarts from the beginning. */
void status_led_set(status_led_pattern_t pattern);

/** Overrides the base pattern until cleared with STATUS_LED_ATTENTION_NONE. */
void status_led_attention(status_led_attention_t attention);

/* The base patterns' colors, for anything that shows them in step with the LED (the screen's
 * Bluetooth symbol). Channels go up to STATUS_LED_PEAK. */
#define STATUS_LED_PEAK 64

typedef struct {
    uint8_t r, g, b;
} status_led_color_t;

/** The color of pattern at ms after it started; done once it stays dark. No side effects. */
status_led_color_t status_led_render(status_led_pattern_t pattern, uint32_t ms, bool *done);

/** The base pattern being shown and the time since it started. Callable from any task. */
void status_led_base(status_led_pattern_t *pattern, uint32_t *elapsed_ms);

#ifdef __cplusplus
}
#endif

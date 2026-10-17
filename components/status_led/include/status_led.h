#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Patterns describe connection state only. Nothing here may ever reflect keystrokes or
 * typing activity: LED timing would leak what is being typed.
 */
typedef enum {
    STATUS_LED_OFF,
    STATUS_LED_ADVERTISING,     /* slow blue pulse */
    STATUS_LED_PAIRING,         /* fast yellow blink */
    STATUS_LED_CONNECTED,       /* green for 0.4 s, fading out by 1 s, then off */
    STATUS_LED_ERROR,           /* three red blinks, then off */
} status_led_pattern_t;

esp_err_t status_led_start(void);

void status_led_set(status_led_pattern_t pattern);

#ifdef __cplusplus
}
#endif

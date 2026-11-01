#include "status_led.h"

#define PULSE_PERIOD_MS     3000
#define BLINK_HALF_MS       125
#define CONNECTED_HOLD_MS   400
#define FADE_MS             600
#define ERROR_BLINKS        3
#define ERROR_HALF_MS       200

#define PEAK STATUS_LED_PEAK

status_led_color_t status_led_render(status_led_pattern_t pattern, uint32_t ms, bool *done)
{
    *done = false;
    switch (pattern) {
    case STATUS_LED_ADVERTISING: {
        uint32_t phase = ms % PULSE_PERIOD_MS;
        uint32_t half = PULSE_PERIOD_MS / 2;
        uint32_t level = phase < half ? phase * PEAK / half : (PULSE_PERIOD_MS - phase) * PEAK / half;
        return (status_led_color_t){ 0, 0, (uint8_t)level };
    }
    case STATUS_LED_PAIRING:
        return (ms / BLINK_HALF_MS) % 2 ? (status_led_color_t){ 0 } : (status_led_color_t){ PEAK, PEAK / 2, 0 };
    case STATUS_LED_CONNECTED: {
        if (ms < CONNECTED_HOLD_MS) {
            return (status_led_color_t){ 0, PEAK, 0 };
        }
        uint32_t t = ms - CONNECTED_HOLD_MS;
        if (t >= FADE_MS) {
            *done = true;
            return (status_led_color_t){ 0 };
        }
        /* Squared ramp: PWM brightness is perceived roughly as its square root, so this looks linear. */
        uint32_t left = FADE_MS - t;
        return (status_led_color_t){ 0, (uint8_t)(PEAK * left * left / (FADE_MS * FADE_MS)), 0 };
    }
    case STATUS_LED_ERROR:
        if (ms >= ERROR_BLINKS * 2 * ERROR_HALF_MS) {
            *done = true;
            return (status_led_color_t){ 0 };
        }
        return (ms / ERROR_HALF_MS) % 2 ? (status_led_color_t){ 0 } : (status_led_color_t){ PEAK, 0, 0 };
    case STATUS_LED_OFF:
    default:
        *done = true;
        return (status_led_color_t){ 0 };
    }
}

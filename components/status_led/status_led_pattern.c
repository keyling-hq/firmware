#include "status_led.h"
#include "status_led_tokens.h"

#define PULSE_PERIOD_MS     3000
#define BLINK_HALF_MS       125
#define CONNECTED_HOLD_MS   400
#define FADE_MS             600
#define ERROR_BLINKS        3
#define ERROR_HALF_MS       200

#define DRIVE_PERIOD_MS     4000
#define DRIVE_LOW           2
#define DRIVE_HIGH          12

#define PEAK STATUS_LED_PEAK

status_led_color_t status_led_color(uint32_t rgb, uint32_t level)
{
    return (status_led_color_t){ (uint8_t)((rgb >> 16 & 0xFF) * level / 255), (uint8_t)((rgb >> 8 & 0xFF) * level / 255),
                                 (uint8_t)((rgb & 0xFF) * level / 255) };
}

status_led_color_t status_led_render(status_led_pattern_t pattern, uint32_t ms, bool *done)
{
    *done = false;
    switch (pattern) {
    case STATUS_LED_ADVERTISING: {
        uint32_t phase = ms % PULSE_PERIOD_MS;
        uint32_t half = PULSE_PERIOD_MS / 2;
        uint32_t level = phase < half ? phase * PEAK / half : (PULSE_PERIOD_MS - phase) * PEAK / half;
        return status_led_color(STATUS_LED_TOKEN_ADVERTISING, level);
    }
    case STATUS_LED_PAIRING:
        return (ms / BLINK_HALF_MS) % 2 ? (status_led_color_t){ 0 } : status_led_color(STATUS_LED_TOKEN_PAIRING, PEAK);
    case STATUS_LED_CONNECTED: {
        if (ms < CONNECTED_HOLD_MS) {
            return status_led_color(STATUS_LED_TOKEN_CONNECTED, PEAK);
        }
        uint32_t t = ms - CONNECTED_HOLD_MS;
        if (t >= FADE_MS) {
            *done = true;
            return (status_led_color_t){ 0 };
        }
        /* Squared ramp: PWM brightness is perceived roughly as its square root, so this looks linear. */
        uint32_t left = FADE_MS - t;
        return status_led_color(STATUS_LED_TOKEN_CONNECTED, PEAK * left * left / (FADE_MS * FADE_MS));
    }
    case STATUS_LED_ERROR:
        if (ms >= ERROR_BLINKS * 2 * ERROR_HALF_MS) {
            *done = true;
            return (status_led_color_t){ 0 };
        }
        return (ms / ERROR_HALF_MS) % 2 ? (status_led_color_t){ 0 } : status_led_color(STATUS_LED_TOKEN_ERROR, PEAK);
    case STATUS_LED_OFF:
    default:
        *done = true;
        return (status_led_color_t){ 0 };
    }
}

/* Breathes between faint and dim, never off: at a glance it reads as "busy", not as a blink. */
status_led_color_t status_led_render_drive(uint32_t ms)
{
    uint32_t phase = ms % DRIVE_PERIOD_MS;
    uint32_t half = DRIVE_PERIOD_MS / 2;
    uint32_t rise = phase < half ? phase : DRIVE_PERIOD_MS - phase;
    uint32_t level = DRIVE_LOW + (DRIVE_HIGH - DRIVE_LOW) * rise / half;
    return status_led_color(STATUS_LED_TOKEN_DRIVE, level);
}

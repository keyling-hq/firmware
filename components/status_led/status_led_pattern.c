#include "status_led.h"
#include "status_led_tokens.h"

#define PULSE_PERIOD_MS     3000
#define BLINK_HALF_MS       125
#define CONNECTED_HOLD_MS   400
#define FADE_MS             600
#define ERROR_BLINKS        3
#define ERROR_HALF_MS       200

#define SWEEP_MS            900
#define SWEEP_GAP_MS        150
#define SWEEPS              2
#define SWEEP_RISE_MS       80
#define SWEEP_FADE_MS       300
#define SWEEP_TURN_MS       (SWEEP_MS - SWEEP_FADE_MS)

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

static uint8_t lerp(uint8_t a, uint8_t b, uint32_t t, uint32_t span)
{
    return (uint8_t)(a + ((int32_t)b - a) * (int32_t)t / (int32_t)span);
}

/* Teal through blue to violet at full brightness, then the violet fades on the squared ramp. */
static status_led_color_t sweep(uint32_t t)
{
    const status_led_color_t stops[] = {
        status_led_color(STATUS_LED_TOKEN_EJECT_START, PEAK),
        status_led_color(STATUS_LED_TOKEN_EJECT_MIDDLE, PEAK),
        status_led_color(STATUS_LED_TOKEN_EJECT_END, PEAK),
    };
    uint32_t half = SWEEP_TURN_MS / 2;
    uint32_t turn = t < SWEEP_TURN_MS ? t : SWEEP_TURN_MS;
    const status_led_color_t *a = &stops[turn < half ? 0 : 1];
    const status_led_color_t *b = a + 1;
    uint32_t u = turn < half ? turn : turn - half;
    status_led_color_t c = { lerp(a->r, b->r, u, half), lerp(a->g, b->g, u, half), lerp(a->b, b->b, u, half) };

    uint32_t level = 256;
    if (t < SWEEP_RISE_MS) {
        level = 256 * t / SWEEP_RISE_MS;
    } else if (t > SWEEP_TURN_MS) {
        uint32_t left = SWEEP_MS - t;
        level = 256 * left * left / (SWEEP_FADE_MS * SWEEP_FADE_MS);
    }
    return (status_led_color_t){ (uint8_t)(c.r * level / 256), (uint8_t)(c.g * level / 256),
                                 (uint8_t)(c.b * level / 256) };
}

status_led_color_t status_led_render_event(status_led_event_t event, uint32_t ms, bool *done)
{
    *done = false;
    switch (event) {
    case STATUS_LED_EVENT_DRIVE_EJECTED: {
        uint32_t period = SWEEP_MS + SWEEP_GAP_MS;
        if (ms >= SWEEPS * period) {
            *done = true;
            return (status_led_color_t){ 0 };
        }
        uint32_t t = ms % period;
        return t < SWEEP_MS ? sweep(t) : (status_led_color_t){ 0 };
    }
    default:
        *done = true;
        return (status_led_color_t){ 0 };
    }
}

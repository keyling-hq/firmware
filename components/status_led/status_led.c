#include "status_led.h"
#include "bsp/tdongle.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"

static const char *TAG = "status_led";

#define TICK_MS             20
#define PEAK                64
#define PULSE_PERIOD_MS     3000
#define BLINK_HALF_MS       125
#define CONNECTED_HOLD_MS   400
#define FADE_MS             600
#define ERROR_BLINKS        3
#define ERROR_HALF_MS       200
#define DOUBLE_BLINK_PERIOD_MS  1200
#define DOUBLE_BLINK_ON_MS      120

typedef struct {
    uint8_t r, g, b;
} rgb_t;

static TaskHandle_t s_task;
static volatile status_led_pattern_t s_pattern;
static volatile status_led_attention_t s_attention;

/* Returns the color for the pattern at elapsed ms, and whether the pattern has finished. */
static rgb_t render(status_led_pattern_t pattern, uint32_t ms, bool *done)
{
    *done = false;
    switch (pattern) {
    case STATUS_LED_ADVERTISING: {
        uint32_t phase = ms % PULSE_PERIOD_MS;
        uint32_t half = PULSE_PERIOD_MS / 2;
        uint32_t level = phase < half ? phase * PEAK / half : (PULSE_PERIOD_MS - phase) * PEAK / half;
        return (rgb_t){ 0, 0, (uint8_t)level };
    }
    case STATUS_LED_PAIRING:
        return (ms / BLINK_HALF_MS) % 2 ? (rgb_t){ 0 } : (rgb_t){ PEAK, PEAK / 2, 0 };
    case STATUS_LED_CONNECTED: {
        if (ms < CONNECTED_HOLD_MS) {
            return (rgb_t){ 0, PEAK, 0 };
        }
        uint32_t t = ms - CONNECTED_HOLD_MS;
        if (t >= FADE_MS) {
            *done = true;
            return (rgb_t){ 0 };
        }
        /* Squared ramp: PWM brightness is perceived roughly as its square root, so this looks linear. */
        uint32_t left = FADE_MS - t;
        return (rgb_t){ 0, (uint8_t)(PEAK * left * left / (FADE_MS * FADE_MS)), 0 };
    }
    case STATUS_LED_ERROR:
        if (ms >= ERROR_BLINKS * 2 * ERROR_HALF_MS) {
            *done = true;
            return (rgb_t){ 0 };
        }
        return (ms / ERROR_HALF_MS) % 2 ? (rgb_t){ 0 } : (rgb_t){ PEAK, 0, 0 };
    case STATUS_LED_OFF:
    default:
        *done = true;
        return (rgb_t){ 0 };
    }
}

/* Attention patterns repeat until cleared. */
static rgb_t render_attention(status_led_attention_t attention, uint32_t ms)
{
    switch (attention) {
    case STATUS_LED_ATTENTION_FIDO: {
        uint32_t phase = ms % DOUBLE_BLINK_PERIOD_MS;
        bool on = phase < DOUBLE_BLINK_ON_MS ||
                  (phase >= 2 * DOUBLE_BLINK_ON_MS && phase < 3 * DOUBLE_BLINK_ON_MS);
        return on ? (rgb_t){ PEAK, 0, PEAK } : (rgb_t){ 0 };
    }
    case STATUS_LED_ATTENTION_NONE:
    default:
        return (rgb_t){ 0 };
    }
}

#define CHANGED_BASE        BIT0
#define CHANGED_ATTENTION   BIT1

static void led_task(void *arg)
{
    status_led_pattern_t pattern = STATUS_LED_OFF;
    status_led_attention_t attention = STATUS_LED_ATTENTION_NONE;
    TickType_t base_start = xTaskGetTickCount();
    TickType_t attention_start = base_start;
    rgb_t shown = { 0 };
    bool idle = true;

    for (;;) {
        uint32_t changed = 0;
        xTaskNotifyWait(0, UINT32_MAX, &changed, idle ? portMAX_DELAY : pdMS_TO_TICKS(TICK_MS));
        TickType_t now = xTaskGetTickCount();
        if (changed & CHANGED_BASE) {
            pattern = s_pattern;
            base_start = now;
        }
        if (changed & CHANGED_ATTENTION) {
            attention = s_attention;
            attention_start = now;
        }

        bool done;
        rgb_t c = render(pattern, pdTICKS_TO_MS(now - base_start), &done);
        if (attention != STATUS_LED_ATTENTION_NONE) {
            c = render_attention(attention, pdTICKS_TO_MS(now - attention_start));
            done = false;
        }
        if (c.r != shown.r || c.g != shown.g || c.b != shown.b) {
            bsp_led_set(c.r, c.g, c.b);
            shown = c;
        }
        idle = done;
    }
}

esp_err_t status_led_start(void)
{
    ESP_RETURN_ON_ERROR(bsp_led_init(), TAG, "led init");
    BaseType_t ok = xTaskCreate(led_task, "status_led", 2048, NULL, 2, &s_task);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

void status_led_set(status_led_pattern_t pattern)
{
    s_pattern = pattern;
    if (s_task) {
        xTaskNotify(s_task, CHANGED_BASE, eSetBits);
    }
}

void status_led_attention(status_led_attention_t attention)
{
    s_attention = attention;
    if (s_task) {
        xTaskNotify(s_task, CHANGED_ATTENTION, eSetBits);
    }
}

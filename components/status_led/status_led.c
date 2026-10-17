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

typedef struct {
    uint8_t r, g, b;
} rgb_t;

static TaskHandle_t s_task;
static volatile status_led_pattern_t s_pattern;

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

static void led_task(void *arg)
{
    status_led_pattern_t pattern = STATUS_LED_OFF;
    TickType_t start = xTaskGetTickCount();
    rgb_t shown = { 0 };
    bool idle = true;

    for (;;) {
        bool changed = ulTaskNotifyTake(pdTRUE, idle ? portMAX_DELAY : pdMS_TO_TICKS(TICK_MS)) > 0;
        if (changed) {
            pattern = s_pattern;
            start = xTaskGetTickCount();
        }

        bool done;
        rgb_t c = render(pattern, pdTICKS_TO_MS(xTaskGetTickCount() - start), &done);
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
        xTaskNotifyGive(s_task);
    }
}

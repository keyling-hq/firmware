#include "status_led.h"
#include "bsp/tdongle.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"

static const char *TAG = "status_led";

#define TICK_MS             20
#define PEAK                STATUS_LED_PEAK
#define DOUBLE_BLINK_PERIOD_MS  1200
#define DOUBLE_BLINK_ON_MS      120

typedef status_led_color_t rgb_t;

static TaskHandle_t s_task;
static volatile status_led_pattern_t s_pattern;
static volatile status_led_attention_t s_attention;
/* The base pattern being shown and when it started, for status_led_base(). */
static volatile status_led_pattern_t s_shown_pattern;
static volatile TickType_t s_shown_start;

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
            s_shown_start = now;
            s_shown_pattern = pattern;
        }
        if (changed & CHANGED_ATTENTION) {
            attention = s_attention;
            attention_start = now;
        }

        bool done;
        rgb_t c = status_led_render(pattern, pdTICKS_TO_MS(now - base_start), &done);
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

void status_led_base(status_led_pattern_t *pattern, uint32_t *elapsed_ms)
{
    TickType_t start = s_shown_start;
    *pattern = s_shown_pattern;
    *elapsed_ms = pdTICKS_TO_MS(xTaskGetTickCount() - start);
}

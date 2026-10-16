#include "bsp/tdongle.h"
#include "driver/gpio.h"
#include "esp_check.h"

static const char *TAG = "bsp_led";

/* APA102 global brightness, 1..31. The LED is very bright, keep it low and fade with RGB. */
#define LED_GLOBAL_BRIGHTNESS   2

static void write_byte(uint8_t b)
{
    for (int i = 7; i >= 0; i--) {
        gpio_set_level(BSP_LED_DATA, (b >> i) & 1);
        gpio_set_level(BSP_LED_CLK, 1);
        gpio_set_level(BSP_LED_CLK, 0);
    }
}

esp_err_t bsp_led_init(void)
{
    const gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << BSP_LED_DATA) | (1ULL << BSP_LED_CLK),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&cfg), TAG, "gpio");
    gpio_set_level(BSP_LED_CLK, 0);
    bsp_led_set(0, 0, 0);
    return ESP_OK;
}

void bsp_led_set(uint8_t r, uint8_t g, uint8_t b)
{
    for (int i = 0; i < 4; i++) {
        write_byte(0x00);
    }
    write_byte(0xE0 | LED_GLOBAL_BRIGHTNESS);
    write_byte(b);
    write_byte(g);
    write_byte(r);
    for (int i = 0; i < 4; i++) {
        write_byte(0xFF);
    }
}

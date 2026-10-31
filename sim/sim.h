#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "iot_button.h"

/* Button: the same event sequence as the button component, from press/release and the clock. */
button_handle_t sim_button_handle(void);
void sim_button_set(bool pressed, uint32_t now_ms);
void sim_button_tick(uint32_t now_ms);

/* Last value from bsp_display_brightness_set(): 0 means the backlight is off. */
int sim_brightness(void);

/* RGB565 pixels written as an RGB PNG, each pixel scaled to zoom x zoom. */
bool sim_png_write(const char *path, const uint16_t *pixels, int width, int height, int zoom, bool dark);

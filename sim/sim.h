#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"
#include "iot_button.h"

/* OS-specific: sim_window.c. Keys are 'A'..'Z' or the codes below. */
#define SIM_KEY_SPACE   0x100
#define SIM_KEY_ESC     0x101
lv_display_t *sim_window_create(int width, int height, int zoom);
bool sim_window_open(void);
bool sim_window_focused(void);
bool sim_key_down(int key);
uint32_t sim_clock_ms(void);
void sim_sleep_ms(uint32_t ms);
void sim_make_dir(const char *path);

/* Button: the same event sequence as the button component, from press/release and the clock. */
button_handle_t sim_button_handle(void);
void sim_button_set(bool pressed, uint32_t now_ms);
void sim_button_tick(uint32_t now_ms);

/* Last value from bsp_display_brightness_set(): 0 means the backlight is off. */
int sim_brightness(void);

/* Design concepts (mockups.c): one contact sheet per direction, drawn on the headless display. */
void sim_mockups_render(const char *out, uint16_t *fb, int zoom);

/* RGB565 pixels written as an RGB PNG, each pixel scaled to zoom x zoom. */
bool sim_png_write(const char *path, const uint16_t *pixels, int width, int height, int zoom, bool dark);

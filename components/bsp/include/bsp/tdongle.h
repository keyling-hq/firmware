#pragma once

#include "esp_err.h"
#include "lvgl.h"
#include "iot_button.h"

#ifdef __cplusplus
extern "C" {
#endif

/* LCD: ST7735, 0.96" 160x80, SPI */
#define BSP_LCD_SPI_HOST        SPI2_HOST
#define BSP_LCD_MOSI            3
#define BSP_LCD_SCLK            5
#define BSP_LCD_CS              4
#define BSP_LCD_DC              2
#define BSP_LCD_RST             1
#define BSP_LCD_BACKLIGHT       38      /* active low */
#define BSP_LCD_H_RES           160
#define BSP_LCD_V_RES           80
#define BSP_LCD_X_GAP           1
#define BSP_LCD_Y_GAP           26

/* BOOT button */
#define BSP_BUTTON_GPIO         0
#define BSP_BUTTON_SHORT_MS     180     /* single/double click window */
#define BSP_BUTTON_LONG_MS      600

/* APA102 RGB LED */
#define BSP_LED_DATA            40
#define BSP_LED_CLK             39

/* QWIIC (not populated on every board) */
#define BSP_QWIIC_TX            43
#define BSP_QWIIC_RX            44

/**
 * Init SPI, the panel, backlight and the LVGL port (which starts the LVGL task).
 * Any LVGL call outside the LVGL task must be wrapped in lvgl_port_lock()/unlock().
 */
esp_err_t bsp_display_start(lv_display_t **out_disp);

esp_err_t bsp_display_brightness_set(int percent);

esp_err_t bsp_button_create(button_handle_t *out_btn);

#ifdef __cplusplus
}
#endif

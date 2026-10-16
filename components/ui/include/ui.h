#pragma once

#include "esp_err.h"
#include "iot_button.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Build the UI and attach the button as LVGL input. Call after bsp_display_start(). */
esp_err_t ui_start(button_handle_t btn);

#ifdef __cplusplus
}
#endif

#include "bsp/tdongle.h"
#include "button_gpio.h"

esp_err_t bsp_button_create(button_handle_t *out_btn)
{
    const button_config_t btn_cfg = {
        .long_press_time = BSP_BUTTON_LONG_MS,
        .short_press_time = BSP_BUTTON_SHORT_MS,
    };
    const button_gpio_config_t gpio_cfg = {
        .gpio_num = BSP_BUTTON_GPIO,
        .active_level = 0,
    };
    return iot_button_new_gpio_device(&btn_cfg, &gpio_cfg, out_btn);
}

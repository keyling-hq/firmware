#include "bsp/tdongle.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "ui.h"

static const char *TAG = "main";

static void nvs_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

void app_main(void)
{
    nvs_init();

    lv_display_t *disp = NULL;
    ESP_ERROR_CHECK(bsp_display_start(&disp));

    button_handle_t btn = NULL;
    ESP_ERROR_CHECK(bsp_button_create(&btn));

    ESP_ERROR_CHECK(ui_start(btn));

    ESP_LOGI(TAG, "started");
}

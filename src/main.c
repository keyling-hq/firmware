#include "app.h"
#include "datafs.h"
#include "esp_log.h"
#include "nvs_flash.h"

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

/*
 * Erasing NVS removes BLE bonds, settings and the keystore salt (and the dev root key), so every
 * derived key changes: anything encrypted with them is gone even if it survived on flash.
 */
static void factory_reset(void)
{
    ESP_LOGW(TAG, "factory reset: erasing NVS and data partition");
    ESP_ERROR_CHECK(nvs_flash_erase());
    ESP_ERROR_CHECK(datafs_format());
}

void app_main(void)
{
    if (app_factory_reset_pending()) {
        factory_reset();
    }
    nvs_init();
    ESP_ERROR_CHECK(app_start());
    ESP_LOGI(TAG, "started");
}

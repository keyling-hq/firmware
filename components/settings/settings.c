#include <string.h>
#include "settings.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_check.h"
#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "settings";

#define NVS_NAMESPACE   "settings"
#define NVS_KEY         "v1"

static const settings_t s_defaults = {
    .layout = "en-US",
    .key_press_ms = 5,
    .key_release_ms = 5,
    .brightness = 80,
    .screen_timeout_s = 15,
    .ble_name = "T-Dongle",
};

const settings_speed_preset_t settings_speed_presets[] = {
    { "Fast", 2, 2 },
    { "Normal", 5, 5 },
    { "Slow", 20, 20 },
};

const size_t settings_speed_preset_count = sizeof(settings_speed_presets) / sizeof(settings_speed_presets[0]);

static settings_t s_settings;
static SemaphoreHandle_t s_lock;

esp_err_t settings_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_lock, ESP_ERR_NO_MEM, TAG, "mutex");
    s_settings = s_defaults;

    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) {
        ESP_LOGI(TAG, "no stored settings, using defaults");
        return ESP_OK;
    }
    settings_t stored;
    size_t len = sizeof(stored);
    esp_err_t err = nvs_get_blob(nvs, NVS_KEY, &stored, &len);
    nvs_close(nvs);
    if (err == ESP_OK && len == sizeof(stored)) {
        stored.layout[SETTINGS_LAYOUT_LEN - 1] = '\0';
        stored.ble_name[SETTINGS_BLE_NAME_LEN - 1] = '\0';
        s_settings = stored;
    } else {
        ESP_LOGW(TAG, "stored settings unreadable (%s), using defaults", esp_err_to_name(err));
    }
    return ESP_OK;
}

void settings_get(settings_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_settings;
    xSemaphoreGive(s_lock);
}

esp_err_t settings_set(const settings_t *in)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_settings = *in;
    xSemaphoreGive(s_lock);

    nvs_handle_t nvs;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs), TAG, "open");
    esp_err_t err = nvs_set_blob(nvs, NVS_KEY, in, sizeof(*in));
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

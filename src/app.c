#include <string.h>
#include "app.h"
#include "bsp/tdongle.h"
#include "esp_check.h"
#include "esp_log.h"
#include "keymap.h"
#include "settings.h"
#include "status_led.h"
#include "typer.h"
#include "ui.h"
#include "usb_hid.h"

static const char *TAG = "app";

static const char TYPE_TEST_TEXT[] =
    "The quick brown fox jumps over the lazy dog. 0123456789\n"
    "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~\n"
    "\xC3\xA1\xC3\xA9\xC3\xAD\xC3\xB3\xC3\xBA \xC3\x81\xC3\x89\xC3\x8D\xC3\x93\xC3\x9A "
    "\xC3\xB1\xC3\x91 \xC3\xBC\xC3\x9C \xC2\xBF\xC2\xA1 \xC2\xB0\xC2\xAC\n";

static const keymap_layout_t *layout_for(const char *id)
{
    const keymap_layout_t *layout = keymap_layout_find(id);
    return layout ? layout : keymap_layout_get(0);
}

static void apply_settings(void)
{
    settings_t settings;
    settings_get(&settings);
    typer_set_layout(layout_for(settings.layout));
    typer_set_timing(settings.key_press_ms, settings.key_release_ms);
}

static void update_settings(void (*change)(settings_t *, int), int arg)
{
    settings_t settings;
    settings_get(&settings);
    change(&settings, arg);
    if (settings_set(&settings) != ESP_OK) {
        ESP_LOGE(TAG, "failed to save settings");
    }
    apply_settings();
}

static void set_layout(settings_t *settings, int index)
{
    const keymap_layout_t *layout = keymap_layout_get(index);
    if (layout) {
        strlcpy(settings->layout, layout->id, sizeof(settings->layout));
    }
}

static void set_speed(settings_t *settings, int index)
{
    if (index >= 0 && (size_t)index < settings_speed_preset_count) {
        settings->key_press_ms = settings_speed_presets[index].key_press_ms;
        settings->key_release_ms = settings_speed_presets[index].key_release_ms;
    }
}

static void ui_action_cb(ui_action_t action, int arg)
{
    switch (action) {
    case UI_ACTION_TYPE_TEST:
        if (typer_text(TYPE_TEST_TEXT, sizeof(TYPE_TEST_TEXT) - 1, 0) != ESP_OK) {
            ESP_LOGW(TAG, "typing queue full");
        }
        break;
    case UI_ACTION_SET_LAYOUT:
        update_settings(set_layout, arg);
        break;
    case UI_ACTION_SET_SPEED:
        update_settings(set_speed, arg);
        break;
    case UI_ACTION_ABORT_TYPING:
        typer_abort();
        break;
    case UI_ACTION_FORGET_PHONE:
        ESP_LOGI(TAG, "forget phone: no BLE bond yet");
        break;
    }
}

static void typer_event_cb(typer_event_t event, void *ctx)
{
    switch (event) {
    case TYPER_EVENT_BUSY:
        ui_show_typing(true);
        break;
    case TYPER_EVENT_IDLE:
        ui_show_typing(false);
        break;
    case TYPER_EVENT_ERROR:
        ui_show_typing(false);
        status_led_set(STATUS_LED_ERROR);
        break;
    }
}

static void usb_mount_cb(bool mounted, void *ctx)
{
    ESP_LOGI(TAG, "usb %s", mounted ? "mounted" : "unmounted");
    ui_set_usb_state(mounted);
    if (mounted) {
        status_led_set(STATUS_LED_CONNECTED);
    } else {
        typer_abort();
    }
}

esp_err_t app_start(void)
{
    ESP_RETURN_ON_ERROR(settings_init(), TAG, "settings");
    ESP_RETURN_ON_ERROR(status_led_start(), TAG, "status led");

    lv_display_t *disp = NULL;
    ESP_RETURN_ON_ERROR(bsp_display_start(&disp), TAG, "display");
    button_handle_t btn = NULL;
    ESP_RETURN_ON_ERROR(bsp_button_create(&btn), TAG, "button");
    ESP_RETURN_ON_ERROR(ui_start(btn, ui_action_cb), TAG, "ui");

    ESP_RETURN_ON_ERROR(typer_start(typer_event_cb, NULL), TAG, "typer");
    apply_settings();
    ESP_RETURN_ON_ERROR(usb_hid_start(usb_mount_cb, NULL), TAG, "usb");
    return ESP_OK;
}

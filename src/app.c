#include <string.h>
#include "app.h"
#include "ble_link.h"
#include "bsp/tdongle.h"
#include "datafs.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_system.h"
#include "keymap.h"
#include "keystore.h"
#include "ota_update.h"
#include "protocol.h"
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

static volatile ble_link_state_t s_ble_state = BLE_LINK_ADVERTISING;
#define MAX_KEY_DELAY_MS    100

static volatile bool s_typing;
static volatile bool s_usb_mounted;
static status_led_pattern_t s_led = STATUS_LED_OFF;

#define FACTORY_RESET_MAGIC 0x52455345u

/* Survives a software restart, unlike regular RAM. */
static RTC_NOINIT_ATTR uint32_t s_factory_reset_request;

void app_factory_reset_request(void)
{
    ESP_LOGW(TAG, "factory reset requested");
    s_factory_reset_request = FACTORY_RESET_MAGIC;
    esp_restart();
}

bool app_factory_reset_pending(void)
{
    bool pending = s_factory_reset_request == FACTORY_RESET_MAGIC
                   && esp_reset_reason() == ESP_RST_SW;
    s_factory_reset_request = 0;
    return pending;
}

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
    protocol_status_changed();
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
        protocol_abort();
        break;
    case UI_ACTION_PAIR_NEW:
        /* Only one connection at a time: the current phone has to make room for the new one. */
        ble_link_disconnect();
        ble_link_pairing_window(true);
        break;
    case UI_ACTION_CANCEL_PAIRING:
        ble_link_pairing_window(false);
        if (s_ble_state == BLE_LINK_SECURING) {
            ble_link_disconnect();
        }
        break;
    case UI_ACTION_FORGET_PHONE:
        ble_link_forget();
        break;
    case UI_ACTION_FACTORY_RESET:
        app_factory_reset_request();
        break;
    }
}

static void notify_phone(const char *msg)
{
    ble_link_notify(msg, strlen(msg));
}

static void typer_event_cb(typer_event_t event, void *ctx)
{
    s_typing = event == TYPER_EVENT_BUSY;
    ui_show_typing(s_typing);
    protocol_status_changed();
    switch (event) {
    case TYPER_EVENT_BUSY:
        break;
    case TYPER_EVENT_IDLE:
        notify_phone("done\n");
        break;
    case TYPER_EVENT_ERROR:
        status_led_set(STATUS_LED_ERROR);
        notify_phone("error\n");
        break;
    }
}

static void usb_mount_cb(bool mounted, void *ctx)
{
    ESP_LOGI(TAG, "usb %s", mounted ? "mounted" : "unmounted");
    s_usb_mounted = mounted;
    ui_set_usb_state(mounted);
    if (!mounted) {
        protocol_abort();
    }
    protocol_status_changed();
}

/* ---- protocol handlers --------------------------------------------------- */

static esp_err_t proto_send(const uint8_t *data, size_t len, void *ctx)
{
    return ble_link_bridge_send(data, len);
}

static void proto_get_config(bridge_Config *out, void *ctx)
{
    settings_t settings;
    settings_get(&settings);
    out->has_layout = true;
    strlcpy(out->layout, settings.layout, sizeof(out->layout));
    out->has_key_press_ms = true;
    out->key_press_ms = settings.key_press_ms;
    out->has_key_release_ms = true;
    out->key_release_ms = settings.key_release_ms;

    size_t max = sizeof(out->available_layouts) / sizeof(out->available_layouts[0]);
    for (size_t i = 0; i < keymap_layout_count() && i < max; i++) {
        strlcpy(out->available_layouts[i], keymap_layout_get(i)->id, sizeof(out->available_layouts[i]));
        out->available_layouts_count = i + 1;
    }
}

static bridge_Result proto_set_config(const bridge_Config *in, void *ctx)
{
    settings_t settings;
    settings_get(&settings);
    if (in->has_layout) {
        if (!keymap_layout_find(in->layout)) {
            return bridge_Result_RESULT_INVALID;
        }
        strlcpy(settings.layout, in->layout, sizeof(settings.layout));
    }
    if (in->has_key_press_ms) {
        if (in->key_press_ms > MAX_KEY_DELAY_MS) {
            return bridge_Result_RESULT_INVALID;
        }
        settings.key_press_ms = in->key_press_ms;
    }
    if (in->has_key_release_ms) {
        if (in->key_release_ms > MAX_KEY_DELAY_MS) {
            return bridge_Result_RESULT_INVALID;
        }
        settings.key_release_ms = in->key_release_ms;
    }
    if (settings_set(&settings) != ESP_OK) {
        return bridge_Result_RESULT_FAILED;
    }
    apply_settings();
    protocol_status_changed();
    return bridge_Result_RESULT_OK;
}

static void proto_get_status(bridge_Status *out, void *ctx)
{
    settings_t settings;
    settings_get(&settings);
    out->typing = s_typing;
    out->usb_mounted = s_usb_mounted;
    strlcpy(out->layout, settings.layout, sizeof(out->layout));
}

static void proto_ota_state(bool active, uint8_t percent, void *ctx)
{
    ui_show_update(active, percent);
}

/* ---- BLE ----------------------------------------------------------------- */

static ui_ble_state_t ui_ble_state_for(const ble_link_status_t *status)
{
    switch (status->state) {
    case BLE_LINK_CONNECTED:
        return UI_BLE_CONNECTED;
    case BLE_LINK_SECURING:
        return UI_BLE_SECURING;
    default:
        return status->pairing_window ? UI_BLE_PAIRING : status->bonded ? UI_BLE_ADVERTISING : UI_BLE_UNPAIRED;
    }
}

/* The connected pattern fades out by itself: it only restarts when a new connection is made. */
static status_led_pattern_t led_for(const ble_link_status_t *status)
{
    if (status->state == BLE_LINK_CONNECTED) {
        return STATUS_LED_CONNECTED;
    }
    if (status->pairing_window) {
        return STATUS_LED_PAIRING;
    }
    if (status->state == BLE_LINK_SECURING) {
        return s_led;
    }
    return status->bonded ? STATUS_LED_OFF : STATUS_LED_ADVERTISING;
}

static void ble_status_cb(const ble_link_status_t *status, void *ctx)
{
    s_ble_state = status->state;
    status_led_pattern_t led = led_for(status);
    if (led != s_led) {
        s_led = led;
        status_led_set(led);
    }
    ui_set_ble_state(ui_ble_state_for(status));
    ui_show_pairing(status->pairing_window);
}

static void ble_passkey_cb(uint32_t passkey, bool show, void *ctx)
{
    ui_show_passkey(passkey, show);
}

static void ble_rx_cb(const uint8_t *data, size_t len, void *ctx)
{
    if (typer_text((const char *)data, len, 0) != ESP_OK) {
        notify_phone("full\n");
    }
}

static void ble_bridge_session_cb(bool active, void *ctx)
{
    protocol_session(active);
}

static void ble_bridge_rx_cb(const uint8_t *data, size_t len, void *ctx)
{
    protocol_rx(data, len);
}

static void ble_bridge_abort_cb(void *ctx)
{
    protocol_abort();
}

esp_err_t app_start(void)
{
    ESP_RETURN_ON_ERROR(keystore_init(), TAG, "keystore");
    ESP_RETURN_ON_ERROR(datafs_mount(), TAG, "data fs");
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

    static const protocol_handlers_t protocol_handlers = {
        .send = proto_send,
        .get_config = proto_get_config,
        .set_config = proto_set_config,
        .get_status = proto_get_status,
        .ota_state = proto_ota_state,
    };
    ESP_RETURN_ON_ERROR(protocol_start(&protocol_handlers, NULL), TAG, "protocol");

    settings_t settings;
    settings_get(&settings);
    static const ble_link_callbacks_t ble_callbacks = {
        .status = ble_status_cb,
        .passkey = ble_passkey_cb,
        .rx = ble_rx_cb,
        .bridge_session = ble_bridge_session_cb,
        .bridge_rx = ble_bridge_rx_cb,
        .bridge_abort = ble_bridge_abort_cb,
    };
    ESP_RETURN_ON_ERROR(ble_link_start(settings.ble_name, &ble_callbacks, NULL), TAG, "ble");

    /* Everything a phone needs to push a fix came up: an updated image is safe to keep. */
    ota_update_confirm_boot();
    return ESP_OK;
}

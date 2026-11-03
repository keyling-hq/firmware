#include <stdio.h>
#include <string.h>
#include "ui_priv.h"
#include "ui_tokens.h"
#include "fw_info.h"
#include "keymap.h"
#include "keystore.h"
#include "settings.h"

static void emit(intptr_t action)
{
    ui_emit((ui_action_t)action, 0);
}

/* ---- keyboard layout ------------------------------------------------------------------------ */

static const char *layout_name(size_t index)
{
    return keymap_layout_get(index)->name;
}

static size_t layout_current(void)
{
    settings_t settings;
    settings_get(&settings);
    for (size_t i = 0; i < keymap_layout_count(); i++) {
        if (strcmp(keymap_layout_get(i)->id, settings.layout) == 0) {
            return i;
        }
    }
    return keymap_layout_count();
}

static void layout_set(size_t index)
{
    ui_emit(UI_ACTION_SET_LAYOUT, (int)index);
}

static const ui_choice_t s_layouts = {
    .count = keymap_layout_count,
    .name = layout_name,
    .current = layout_current,
    .set = layout_set,
};

/* ---- typing speed --------------------------------------------------------------------------- */

static size_t speed_count(void)
{
    return settings_speed_preset_count;
}

static const char *speed_name(size_t index)
{
    return settings_speed_presets[index].name;
}

static size_t speed_current(void)
{
    settings_t settings;
    settings_get(&settings);
    for (size_t i = 0; i < settings_speed_preset_count; i++) {
        if (settings_speed_presets[i].key_press_ms == settings.key_press_ms) {
            return i;
        }
    }
    return settings_speed_preset_count;
}

static void speed_set(size_t index)
{
    ui_emit(UI_ACTION_SET_SPEED, (int)index);
}

/* Set from the phone, the timing may match no preset. */
static const char *speed_value(intptr_t arg)
{
    size_t current = speed_current();
    return current < settings_speed_preset_count ? speed_name(current) : "Custom";
}

static const ui_choice_t s_speeds = {
    .count = speed_count,
    .name = speed_name,
    .current = speed_current,
    .set = speed_set,
};

/* ---- theme ---------------------------------------------------------------------------------- */

static size_t theme_count(void)
{
    size_t count = 0;
    while (ui_theme_name(count)) {
        count++;
    }
    return count;
}

static const ui_choice_t s_themes = {
    .count = theme_count,
    .name = ui_theme_name,
    .current = ui_theme_index,
    .set = ui_theme_select,
};

/* ---- status LED ----------------------------------------------------------------------------- */

static const char *const s_led_level_names[] = { "All", "Important", "Off" };
#define LED_LEVEL_COUNT (sizeof(s_led_level_names) / sizeof(s_led_level_names[0]))

static size_t led_level_count(void)
{
    return LED_LEVEL_COUNT;
}

static const char *led_level_name(size_t index)
{
    return s_led_level_names[index];
}

static size_t led_level_current(void)
{
    settings_t settings;
    settings_get(&settings);
    return settings.led_level < LED_LEVEL_COUNT ? settings.led_level : 0;
}

static void led_level_set(size_t index)
{
    ui_emit(UI_ACTION_SET_LED_LEVEL, (int)index);
}

static const ui_choice_t s_led_levels = {
    .count = led_level_count,
    .name = led_level_name,
    .current = led_level_current,
    .set = led_level_set,
};

/* ---- phone and reset ------------------------------------------------------------------------ */

static void forget(intptr_t arg)
{
    ui_emit(UI_ACTION_FORGET_PHONE, 0);
}

static const ui_confirm_t s_forget = {
    .title = "Forget phone?",
    .message = "Pair again to reconnect",
    .confirm = "Forget",
    .fn = forget,
};

static void factory_reset(intptr_t arg)
{
    ui_emit(UI_ACTION_FACTORY_RESET, 0);
}

static const ui_confirm_t s_reset = {
    .title = "Erase everything?",
    .message = "Phone, settings, data",
    .confirm = "Hold to erase",
    .hold_ms = UI_HOLD_RESET_MS,
    .fn = factory_reset,
};

/* ---- about ---------------------------------------------------------------------------------- */

static const char *ble_text(void)
{
    switch (ui_ble_state()) {
    case UI_BLE_CONNECTED:
        return "connected";
    case UI_BLE_SECURING:
        return "connecting";
    case UI_BLE_PAIRING:
        return "pairing";
    case UI_BLE_UNPAIRED:
        return "not paired";
    default:
        return "waiting";
    }
}

static void about(char *text, size_t len)
{
    settings_t settings;
    settings_get(&settings);
    keystore_status_t ks;
    keystore_status(&ks);
    ui_emit(UI_ACTION_LOG_DIAGNOSTICS, 0);
#ifdef CONFIG_SECURE_SIGNED_ON_UPDATE
    const char *updates = "signed";
#else
    const char *updates = "any";
#endif
    snprintf(text, len,
             "Keyling %s\n"
             "Profile: %s%s\n"
             "Phone: %s\n"
             "USB: %s\n"
             "Name: %s\n"
             "Security key: %s\n"
             "Keys: %s\n"
             "Secure boot: %s\n"
             "Flash encryption: %s\n"
             "Updates: %s",
             fw_info_version(), ui_profile_name(ui_profile_active()),
             ui_profile_active() == UI_PROFILE_PHANTOM && ui_phantom_state() != UI_PHANTOM_OPEN ? " (locked)" : "",
             ble_text(), ui_usb_mounted() ? "connected" : "not connected", settings.ble_name, ui_fido_status(),
             ks.root == KEYSTORE_ROOT_EFUSE ? "eFuse" : "development", ks.secure_boot ? "on" : "off",
             ks.flash_encryption ? "on" : "off", updates);
}

/* ---- the menu ------------------------------------------------------------------------------- */

static const ui_option_t s_settings[] = {
    UI_ACTION_ARG("Type test", emit, UI_ACTION_TYPE_TEST),
    UI_CHOICE("Layout", &s_layouts),
    UI_OPTION(.kind = UI_OPT_CHOICE, .text = "Speed", .choice = &s_speeds, .value_fn = speed_value),
    UI_CHOICE("Theme", &s_themes),
    UI_CHOICE("LED", &s_led_levels),
    UI_ACTION_ARG("Pair new phone", emit, UI_ACTION_PAIR_NEW),
    UI_CONFIRM("Forget phone", &s_forget),
    UI_PAGE("About", about),
    UI_CONFIRM("Factory reset", &s_reset),
    UI_EXIT("Hold to exit"),
};
static UI_MENU(s_settings_menu, "Settings", s_settings);

const ui_app_t ui_app_settings = {
    .id = "settings",
    .name = "Settings",
    .icon = LV_SYMBOL_SETTINGS,
    .color = UI_TOKEN_APP_SETTINGS,
    .menu = &s_settings_menu,
};

#include <stdio.h>
#include <string.h>
#include "ui_priv.h"
#include "fw_info.h"
#include "keymap.h"
#include "keystore.h"
#include "settings.h"

static void show_settings(void);

static void action_click_cb(lv_event_t *e)
{
    ui_emit((ui_action_t)(intptr_t)lv_event_get_user_data(e), 0);
}

static void layout_select_cb(lv_event_t *e)
{
    ui_emit(UI_ACTION_SET_LAYOUT, (int)(intptr_t)lv_event_get_user_data(e));
    ui_navigate(show_settings);
}

static void speed_select_cb(lv_event_t *e)
{
    ui_emit(UI_ACTION_SET_SPEED, (int)(intptr_t)lv_event_get_user_data(e));
    ui_navigate(show_settings);
}

static void theme_select_cb(lv_event_t *e)
{
    ui_theme_select((size_t)(intptr_t)lv_event_get_user_data(e));
    ui_navigate(show_settings);
}

static void forget_confirm_cb(lv_event_t *e)
{
    ui_emit(UI_ACTION_FORGET_PHONE, 0);
    ui_navigate(show_settings);
}

static void show_layout_menu(void)
{
    settings_t settings;
    settings_get(&settings);
    ui_menu_t *menu = ui_menu_begin("Keyboard layout", show_settings);
    lv_obj_t *focus = NULL;
    for (size_t i = 0; i < keymap_layout_count(); i++) {
        const keymap_layout_t *layout = keymap_layout_get(i);
        lv_obj_t *item = ui_menu_add(menu, layout->name, layout_select_cb, (intptr_t)i);
        if (strcmp(layout->id, settings.layout) == 0) {
            ui_menu_mark(menu, item);
            focus = item;
        }
    }
    ui_menu_end(menu, focus);
}

static void show_speed_menu(void)
{
    settings_t settings;
    settings_get(&settings);
    ui_menu_t *menu = ui_menu_begin("Typing speed", show_settings);
    lv_obj_t *focus = NULL;
    for (size_t i = 0; i < settings_speed_preset_count; i++) {
        const settings_speed_preset_t *preset = &settings_speed_presets[i];
        lv_obj_t *item = ui_menu_add(menu, preset->name, speed_select_cb, (intptr_t)i);
        if (preset->key_press_ms == settings.key_press_ms) {
            ui_menu_mark(menu, item);
            focus = item;
        }
    }
    ui_menu_end(menu, focus);
}

static void show_theme_menu(void)
{
    ui_menu_t *menu = ui_menu_begin("Theme", show_settings);
    lv_obj_t *focus = NULL;
    for (size_t i = 0; ui_theme_name(i); i++) {
        lv_obj_t *item = ui_menu_add(menu, ui_theme_name(i), theme_select_cb, (intptr_t)i);
        if (i == ui_theme_index()) {
            ui_menu_mark(menu, item);
            focus = item;
        }
    }
    ui_menu_end(menu, focus);
}

static void show_forget_menu(void)
{
    ui_menu_t *dialog = ui_dialog_begin("Forget phone?", "Pair again to reconnect", show_settings);
    lv_obj_t *cancel = ui_menu_add(dialog, "Cancel", ui_submenu_cb, (intptr_t)show_settings);
    ui_menu_add(dialog, "Forget", forget_confirm_cb, 0);
    ui_menu_end(dialog, cancel);
}

static void factory_reset(void)
{
    ui_emit(UI_ACTION_FACTORY_RESET, 0);
}

static void show_reset_menu(void)
{
    ui_menu_t *dialog = ui_dialog_begin("Erase everything?", "Phone, settings, data", show_settings);
    lv_obj_t *cancel = ui_menu_add(dialog, "Cancel", ui_submenu_cb, (intptr_t)show_settings);
    ui_hold_add(dialog, "Hold to erase", UI_HOLD_RESET_MS, factory_reset);
    ui_menu_end(dialog, cancel);
}

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

static void show_about(void)
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
    static char text[384];
    snprintf(text, sizeof(text),
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
    ui_text_show("About", text, show_settings);
}

static void show_settings(void)
{
    settings_t settings;
    settings_get(&settings);

    ui_menu_t *menu = ui_menu_begin("Settings", ui_app_exit);
    ui_menu_add(menu, "Type test", action_click_cb, UI_ACTION_TYPE_TEST);
    const keymap_layout_t *layout = keymap_layout_find(settings.layout);
    ui_menu_add_value(menu, "Layout", layout ? layout->name : "?", ui_submenu_cb, (intptr_t)show_layout_menu);
    const char *speed = "Custom";
    for (size_t i = 0; i < settings_speed_preset_count; i++) {
        if (settings_speed_presets[i].key_press_ms == settings.key_press_ms) {
            speed = settings_speed_presets[i].name;
        }
    }
    ui_menu_add_value(menu, "Speed", speed, ui_submenu_cb, (intptr_t)show_speed_menu);
    ui_menu_add_value(menu, "Theme", ui_theme_name(ui_theme_index()), ui_submenu_cb, (intptr_t)show_theme_menu);
    ui_menu_add(menu, "Pair new phone", action_click_cb, UI_ACTION_PAIR_NEW);
    ui_menu_add(menu, "Forget phone", ui_submenu_cb, (intptr_t)show_forget_menu);
    ui_menu_add(menu, "About", ui_submenu_cb, (intptr_t)show_about);
    ui_menu_add(menu, "Factory reset", ui_submenu_cb, (intptr_t)show_reset_menu);
    ui_hold_add(menu, "Hold to exit", UI_HOLD_EXIT_MS, ui_app_exit);
    ui_menu_end(menu, NULL);
}

const ui_app_t ui_app_settings = {
    .id = "settings",
    .name = "Settings",
    .icon = LV_SYMBOL_SETTINGS,
    .color = 0xFFD166,
    .show = show_settings,
};

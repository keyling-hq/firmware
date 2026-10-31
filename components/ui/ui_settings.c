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

static void forget_confirm_cb(lv_event_t *e)
{
    ui_emit(UI_ACTION_FORGET_PHONE, 0);
    ui_navigate(show_settings);
}

static void show_layout_menu(void)
{
    settings_t settings;
    settings_get(&settings);
    lv_obj_t *list = ui_menu_begin("Keyboard layout", show_settings);
    lv_obj_t *focus = NULL;
    for (size_t i = 0; i < keymap_layout_count(); i++) {
        const keymap_layout_t *layout = keymap_layout_get(i);
        lv_obj_t *item = ui_menu_add(list, layout->name, layout_select_cb, (intptr_t)i);
        if (strcmp(layout->id, settings.layout) == 0) {
            focus = item;
        }
    }
    ui_menu_end(list, focus);
}

static void show_speed_menu(void)
{
    settings_t settings;
    settings_get(&settings);
    lv_obj_t *list = ui_menu_begin("Typing speed", show_settings);
    lv_obj_t *focus = NULL;
    for (size_t i = 0; i < settings_speed_preset_count; i++) {
        const settings_speed_preset_t *preset = &settings_speed_presets[i];
        lv_obj_t *item = ui_menu_add(list, preset->name, speed_select_cb, (intptr_t)i);
        if (preset->key_press_ms == settings.key_press_ms) {
            focus = item;
        }
    }
    ui_menu_end(list, focus);
}

static void show_forget_menu(void)
{
    lv_obj_t *list = ui_menu_begin("Forget phone?", show_settings);
    ui_menu_add(list, "Cancel", ui_submenu_cb, (intptr_t)show_settings);
    ui_menu_add(list, "Forget", forget_confirm_cb, 0);
    ui_menu_end(list, NULL);
}

static void factory_reset(void)
{
    ui_emit(UI_ACTION_FACTORY_RESET, 0);
}

static void show_reset_menu(void)
{
    lv_obj_t *list = ui_menu_begin("Erase everything?", show_settings);
    lv_obj_t *info = lv_label_create(list);
    lv_label_set_text(info, "Phone, settings, data");
    lv_obj_t *cancel = ui_menu_add(list, "Cancel", ui_submenu_cb, (intptr_t)show_settings);
    ui_hold_add(list, "Hold to erase", UI_HOLD_RESET_MS, factory_reset);
    ui_menu_end(list, cancel);
}

static void show_about(void)
{
    keystore_status_t ks;
    keystore_status(&ks);
    ui_emit(UI_ACTION_LOG_DIAGNOSTICS, 0);
    char title[40];
    snprintf(title, sizeof(title), "About  FIDO %s", ui_fido_status());
    lv_obj_t *list = ui_menu_begin(title, show_settings);
    lv_obj_t *info = lv_label_create(list);
#ifdef CONFIG_SECURE_SIGNED_ON_UPDATE
    const char *updates = "signed";
#else
    const char *updates = "any";
#endif
    lv_label_set_text_fmt(info, "Keyling %s\n%s\nkeys %s, boot %s, enc %s\nupdates %s",
                          fw_info_version(),
                          keystore_is_secure() ? "Secure" : "INSECURE (dev)",
                          ks.root == KEYSTORE_ROOT_EFUSE ? "efuse" : "dev",
                          ks.secure_boot ? "on" : "off", ks.flash_encryption ? "on" : "off", updates);
    ui_menu_add(list, "Back", ui_submenu_cb, (intptr_t)show_settings);
    ui_menu_end(list, NULL);
}

static void show_settings(void)
{
    settings_t settings;
    settings_get(&settings);
    char text[32];

    lv_obj_t *list = ui_menu_begin("Settings", ui_app_exit);
    ui_menu_add(list, "Type test", action_click_cb, UI_ACTION_TYPE_TEST);
    const keymap_layout_t *layout = keymap_layout_find(settings.layout);
    snprintf(text, sizeof(text), "Layout: %s", layout ? layout->name : "?");
    ui_menu_add(list, text, ui_submenu_cb, (intptr_t)show_layout_menu);
    const char *speed = "Custom";
    for (size_t i = 0; i < settings_speed_preset_count; i++) {
        if (settings_speed_presets[i].key_press_ms == settings.key_press_ms) {
            speed = settings_speed_presets[i].name;
        }
    }
    snprintf(text, sizeof(text), "Speed: %s", speed);
    ui_menu_add(list, text, ui_submenu_cb, (intptr_t)show_speed_menu);
    ui_menu_add(list, "Pair new phone", action_click_cb, UI_ACTION_PAIR_NEW);
    ui_menu_add(list, "Forget phone", ui_submenu_cb, (intptr_t)show_forget_menu);
    ui_menu_add(list, "About", ui_submenu_cb, (intptr_t)show_about);
    ui_menu_add(list, "Factory reset", ui_submenu_cb, (intptr_t)show_reset_menu);
    ui_hold_add(list, "Hold to exit", UI_HOLD_EXIT_MS, ui_app_exit);
    ui_menu_end(list, NULL);
}

const ui_app_t ui_app_settings = {
    .id = "settings",
    .name = "Settings",
    .icon = LV_SYMBOL_SETTINGS,
    .show = show_settings,
};

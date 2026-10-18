#include <stdio.h>
#include <string.h>
#include "ui_priv.h"
#include "esp_idf_version.h"
#include "keymap.h"
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

static void show_about(void)
{
    lv_obj_t *list = ui_menu_begin("About", show_settings);
    lv_obj_t *info = lv_label_create(list);
    lv_label_set_text_fmt(info, "Keyling\nIDF %s", esp_get_idf_version());
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
    ui_hold_add(list, "Hold to exit", UI_HOLD_EXIT_MS, ui_app_exit);
    ui_menu_end(list, NULL);
}

const ui_app_t ui_app_settings = {
    .name = "Settings",
    .icon = LV_SYMBOL_SETTINGS,
    .show = show_settings,
};

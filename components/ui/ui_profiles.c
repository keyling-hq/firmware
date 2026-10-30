#include <stdio.h>
#include "ui_priv.h"

static void show_profiles(void);

static ui_profile_t s_reset_profile;

static const char *state_text(ui_profile_t profile)
{
    if (profile != UI_PROFILE_PHONE_LOCKED) {
        return "";
    }
    switch (ui_phone_locked_state()) {
    case UI_PHONE_LOCKED_NEW:
        return ": new";
    case UI_PHONE_LOCKED_WAITING:
        return ": waiting";
    default:
        return "";
    }
}

static void select_cb(lv_event_t *e)
{
    ui_emit(UI_ACTION_SELECT_PROFILE, (int)(intptr_t)lv_event_get_user_data(e));
}

static void default_cb(lv_event_t *e)
{
    ui_profile_t other = ui_profile_default() == UI_PROFILE_MAIN ? UI_PROFILE_PHONE_LOCKED : UI_PROFILE_MAIN;
    ui_emit(UI_ACTION_DEFAULT_PROFILE, other);
}

static void reset_profile(void)
{
    ui_emit(UI_ACTION_RESET_PROFILE, s_reset_profile);
    ui_navigate(show_profiles);
}

static void show_reset_menu(void)
{
    char title[32];
    snprintf(title, sizeof(title), "Erase %s?", ui_profile_name(s_reset_profile));
    lv_obj_t *list = ui_menu_begin(title, show_profiles);
    lv_obj_t *cancel = ui_menu_add(list, "Cancel", ui_submenu_cb, (intptr_t)show_profiles);
    ui_hold_add(list, "Hold to erase", UI_HOLD_RESET_MS, reset_profile);
    ui_menu_end(list, cancel);
}

static void reset_cb(lv_event_t *e)
{
    s_reset_profile = (ui_profile_t)(intptr_t)lv_event_get_user_data(e);
    ui_navigate(show_reset_menu);
}

static void show_profiles(void)
{
    lv_obj_t *list = ui_menu_begin("Profiles", NULL);
    lv_obj_t *focus = NULL;
    char text[40];
    for (int i = 0; i < UI_PROFILE_COUNT; i++) {
        bool active = i == (int)ui_profile_active();
        snprintf(text, sizeof(text), "%s%s%s", active ? LV_SYMBOL_OK " " : "", ui_profile_name(i), state_text(i));
        lv_obj_t *item = ui_menu_add(list, text, select_cb, i);
        if (active) {
            focus = item;
        }
    }
    snprintf(text, sizeof(text), "Start: %s", ui_profile_name(ui_profile_default()));
    ui_menu_add(list, text, default_cb, 0);
    for (int i = 0; i < UI_PROFILE_COUNT; i++) {
        snprintf(text, sizeof(text), "Erase %s", ui_profile_name(i));
        ui_menu_add(list, text, reset_cb, i);
    }
    ui_hold_add(list, "Hold to exit", UI_HOLD_EXIT_MS, ui_app_exit);
    ui_menu_end(list, focus);
}

const ui_app_t ui_app_profiles = {
    .id = "profiles",
    .name = "Profiles",
    .icon = LV_SYMBOL_LIST,
    .show = show_profiles,
};

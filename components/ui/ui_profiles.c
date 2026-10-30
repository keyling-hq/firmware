#include <stdio.h>
#include "ui_priv.h"

static void show_profiles(void);
static void show_profile(void);

/* The profile whose menu is open, and the screen to rebuild when the profiles change. */
static ui_profile_t s_profile;
static ui_screen_fn_t s_screen = show_profiles;

static ui_profile_t other_profile(ui_profile_t profile)
{
    return profile == UI_PROFILE_MAIN ? UI_PROFILE_PHANTOM : UI_PROFILE_MAIN;
}

static const char *state_text(ui_profile_t profile)
{
    if (profile != UI_PROFILE_PHANTOM) {
        return "";
    }
    switch (ui_phantom_state()) {
    case UI_PHANTOM_NEW:
        return ": new";
    case UI_PHANTOM_WAITING:
        return ": waiting";
    default:
        return "";
    }
}

static void mark_active(lv_obj_t *item)
{
    lv_obj_set_style_bg_color(item, lv_palette_darken(LV_PALETTE_GREEN, 2), 0);
}

/* ---- one profile's menu -------------------------------------------------- */

static void use_cb(lv_event_t *e)
{
    ui_emit(UI_ACTION_SELECT_PROFILE, s_profile);
    ui_navigate(show_profiles);
}

static void startup_cb(lv_event_t *e)
{
    ui_emit(UI_ACTION_DEFAULT_PROFILE, s_profile);
}

static void erase(void)
{
    ui_emit(UI_ACTION_RESET_PROFILE, s_profile);
    ui_navigate(show_profile);
}

static void show_erase(void)
{
    s_screen = show_erase;
    char title[32];
    snprintf(title, sizeof(title), "Erase %s?", ui_profile_name(s_profile));
    lv_obj_t *list = ui_menu_begin(title, show_profile);
    lv_obj_t *cancel = ui_menu_add(list, "Cancel", ui_submenu_cb, (intptr_t)show_profile);
    ui_hold_add(list, "Hold to erase", UI_HOLD_RESET_MS, erase);
    ui_menu_end(list, cancel);
}

static void show_profile(void)
{
    s_screen = show_profile;
    char title[40];
    snprintf(title, sizeof(title), "%s%s", ui_profile_name(s_profile), state_text(s_profile));
    lv_obj_t *list = ui_menu_begin(title, show_profiles);
    if (s_profile == ui_profile_active()) {
        mark_active(ui_menu_add(list, LV_SYMBOL_OK " In use", NULL, 0));
    } else {
        ui_menu_add(list, "Use", use_cb, 0);
    }
    bool startup = s_profile == ui_profile_default();
    ui_menu_add(list, startup ? LV_SYMBOL_OK " Start with this" : "Start with this", startup ? NULL : startup_cb, 0);
    ui_menu_add(list, "Erase", ui_submenu_cb, (intptr_t)show_erase);
    ui_menu_end(list, NULL);
}

/* ---- top level ------------------------------------------------------------ */

static void switch_cb(lv_event_t *e)
{
    ui_emit(UI_ACTION_SELECT_PROFILE, other_profile(ui_profile_active()));
}

static void open_cb(lv_event_t *e)
{
    s_profile = (ui_profile_t)(intptr_t)lv_event_get_user_data(e);
    ui_navigate(show_profile);
}

static void show_profiles(void)
{
    s_screen = show_profiles;
    lv_obj_t *list = ui_menu_begin("Profiles", ui_app_exit);
    char text[40];
    snprintf(text, sizeof(text), "Switch to %s", ui_profile_name(other_profile(ui_profile_active())));
    lv_obj_t *focus = ui_menu_add(list, text, switch_cb, 0);
    for (int i = 0; i < UI_PROFILE_COUNT; i++) {
        snprintf(text, sizeof(text), "%s%s", ui_profile_name(i), state_text(i));
        lv_obj_t *item = ui_menu_add(list, text, open_cb, i);
        if (i == (int)ui_profile_active()) {
            mark_active(item);
        }
    }
    ui_menu_end(list, focus);
}

static void start(void)
{
    s_screen = show_profiles;
}

static void refresh(void)
{
    s_screen();
}

const ui_app_t ui_app_profiles = {
    .id = "profiles",
    .name = "Profiles",
    .icon = LV_SYMBOL_LIST,
    .start = start,
    .show = show_profiles,
    .refresh = refresh,
};

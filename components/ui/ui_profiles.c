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
        return "new";
    case UI_PHANTOM_WAITING:
        return "waiting";
    default:
        return "";
    }
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
    ui_menu_t *dialog = ui_dialog_begin(title, "Its keys are deleted", show_profile);
    lv_obj_t *cancel = ui_menu_add(dialog, "Cancel", ui_submenu_cb, (intptr_t)show_profile);
    ui_hold_add(dialog, "Hold to erase", UI_HOLD_RESET_MS, erase);
    ui_menu_end(dialog, cancel);
}

static void show_profile(void)
{
    s_screen = show_profile;
    ui_menu_t *menu = ui_menu_begin(ui_profile_name(s_profile), show_profiles);
    if (s_profile == ui_profile_active()) {
        ui_menu_mark(menu, ui_menu_add(menu, "In use", NULL, 0));
    } else {
        ui_menu_add(menu, "Use", use_cb, 0);
    }
    bool startup = s_profile == ui_profile_default();
    lv_obj_t *item = ui_menu_add(menu, "Start with this", startup ? NULL : startup_cb, 0);
    if (startup) {
        ui_menu_mark(menu, item);
    }
    ui_menu_add(menu, "Erase", ui_submenu_cb, (intptr_t)show_erase);
    ui_menu_end(menu, NULL);
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
    ui_menu_t *menu = ui_menu_begin("Profiles", ui_app_exit);
    char text[40];
    snprintf(text, sizeof(text), "Switch to %s", ui_profile_name(other_profile(ui_profile_active())));
    lv_obj_t *focus = ui_menu_add(menu, text, switch_cb, 0);
    for (int i = 0; i < UI_PROFILE_COUNT; i++) {
        lv_obj_t *item = ui_menu_add_value(menu, ui_profile_name(i), state_text(i), open_cb, i);
        if (i == (int)ui_profile_active()) {
            ui_menu_mark(menu, item);
        }
    }
    ui_menu_end(menu, focus);
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
    .color = 0x4ECDC4,
    .start = start,
    .show = show_profiles,
    .refresh = refresh,
};

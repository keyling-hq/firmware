#include <stdio.h>
#include "ui_priv.h"
#include "ui_tokens.h"

static ui_profile_t other_profile(ui_profile_t profile)
{
    return profile == UI_PROFILE_MAIN ? UI_PROFILE_PHANTOM : UI_PROFILE_MAIN;
}

static bool is_active(intptr_t profile)
{
    return (ui_profile_t)profile == ui_profile_active();
}

static const char *state_value(intptr_t profile)
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

/* ---- one profile's menu: ui_menu_arg() is the profile ------------------------------------- */

static const char *profile_title(intptr_t profile)
{
    return ui_profile_name(profile);
}

static bool open_is_active(intptr_t arg)
{
    return is_active(ui_menu_arg());
}

static const char *use_text(intptr_t arg)
{
    return open_is_active(arg) ? "In use" : "Use";
}

static void use(intptr_t arg)
{
    if (!open_is_active(arg)) {
        ui_emit(UI_ACTION_SELECT_PROFILE, (int)ui_menu_arg());
        ui_back(1);
    }
}

static bool is_startup(intptr_t arg)
{
    return (ui_profile_t)ui_menu_arg() == ui_profile_default();
}

static void make_startup(intptr_t arg)
{
    ui_emit(UI_ACTION_DEFAULT_PROFILE, (int)ui_menu_arg());
}

static const char *erase_title(intptr_t profile)
{
    static char title[32];
    snprintf(title, sizeof(title), "Erase %s?", ui_profile_name(profile));
    return title;
}

static void erase(intptr_t profile)
{
    ui_emit(UI_ACTION_RESET_PROFILE, (int)profile);
}

static const ui_confirm_t s_erase = {
    .title_fn = erase_title,
    .message = "Its keys are deleted",
    .confirm = "Hold to erase",
    .hold_ms = UI_HOLD_RESET_MS,
    .fn = erase,
};

static const ui_option_t s_profile[] = {
    UI_OPTION(.kind = UI_OPT_ACTION, .text_fn = use_text, .marked_fn = open_is_active, .fn = use),
    UI_OPTION(.kind = UI_OPT_ACTION, .text = "Start with this", .marked_fn = is_startup, .fn = make_startup),
    UI_CONFIRM("Erase", &s_erase),
};
static UI_MENU_DYN(s_profile_menu, profile_title, s_profile);

/* ---- top level ---------------------------------------------------------------------------- */

static const char *switch_text(intptr_t arg)
{
    static char text[32];
    snprintf(text, sizeof(text), "Switch to %s", ui_profile_name(other_profile(ui_profile_active())));
    return text;
}

static void switch_profile(intptr_t arg)
{
    ui_emit(UI_ACTION_SELECT_PROFILE, other_profile(ui_profile_active()));
}

static const ui_option_t s_profiles[] = {
    UI_OPTION(.kind = UI_OPT_ACTION, .text_fn = switch_text, .fn = switch_profile),
    UI_OPTION(.kind = UI_OPT_SUBMENU, .text = "Main", .submenu = &s_profile_menu, .arg = UI_PROFILE_MAIN,
              .value_fn = state_value, .marked_fn = is_active),
    UI_OPTION(.kind = UI_OPT_SUBMENU, .text = "Phantom", .submenu = &s_profile_menu, .arg = UI_PROFILE_PHANTOM,
              .value_fn = state_value, .marked_fn = is_active),
};
static UI_MENU(s_profiles_menu, "Profiles", s_profiles);

const ui_app_t ui_app_profiles = {
    .id = "profiles",
    .name = "Profiles",
    .icon = LV_SYMBOL_LIST,
    .color = UI_TOKEN_APP_PROFILES,
    .menu = &s_profiles_menu,
};

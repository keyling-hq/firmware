/*
 * Example app, and the starting point for new ones: a counter described with the menu framework
 * (ui_options.h). It uses every kind of option. Built only with CONFIG_UI_APP_EXAMPLE.
 *
 * To add an app: copy this file, give it its own id, name, icon and color, add the file to
 * CMakeLists.txt and the app to s_apps in ui.c. The framework builds the screens in the active
 * theme and handles the button: click moves to the next option, hold activates it, double click
 * goes back (and leaves the app from its first menu).
 */
#include <stdio.h>
#include "ui_priv.h"

static int s_count;
static int s_step = 1;
static bool s_wrap;

/* ---- an action with its value beside it --------------------------------------------------- */

static void add(intptr_t arg)
{
    s_count += s_step;
    if (s_wrap && s_count > 99) {
        s_count = 0;
    }
}

static const char *count_value(intptr_t arg)
{
    static char text[12];
    snprintf(text, sizeof(text), "%d", s_count);
    return text;
}

/* ---- a choice: the option shows the current name and opens the list ----------------------- */

static const int s_steps[] = { 1, 5, 10 };

static size_t step_count(void)
{
    return sizeof(s_steps) / sizeof(s_steps[0]);
}

static const char *step_name(size_t index)
{
    static const char *const names[] = { "1", "5", "10" };
    return names[index];
}

static size_t step_current(void)
{
    for (size_t i = 0; i < step_count(); i++) {
        if (s_steps[i] == s_step) {
            return i;
        }
    }
    return 0;
}

static void step_set(size_t index)
{
    s_step = s_steps[index];
}

static const ui_choice_t s_step_choice = {
    .count = step_count,
    .name = step_name,
    .current = step_current,
    .set = step_set,
};

/* ---- a toggle, in a submenu --------------------------------------------------------------- */

static bool wrap_get(void)
{
    return s_wrap;
}

static void wrap_set(bool on)
{
    s_wrap = on;
}

static const ui_option_t s_advanced[] = {
    UI_TOGGLE("Wrap at 100", wrap_get, wrap_set),
};
static UI_MENU(s_advanced_menu, "Advanced", s_advanced);

/* ---- a confirmation and a page ------------------------------------------------------------ */

static void reset(intptr_t arg)
{
    s_count = 0;
}

static const ui_confirm_t s_reset = {
    .title = "Reset counter?",
    .message = "Back to zero",
    .confirm = "Hold to reset",
    .hold_ms = UI_HOLD_CONFIRM_MS,
    .fn = reset,
};

static void help(char *text, size_t len)
{
    snprintf(text, len,
             "Click moves to the next option.\n"
             "Hold activates it.\n"
             "Double click goes back.\n"
             "Hold to reset fills while held.\n"
             "Long text like this scrolls up slowly.");
}

/* ---- the root menu ------------------------------------------------------------------------ */

static const ui_option_t s_main[] = {
    UI_OPTION(.kind = UI_OPT_ACTION, .text = "Add", .fn = add, .value_fn = count_value),
    UI_CHOICE("Step", &s_step_choice),
    UI_SUBMENU("Advanced", &s_advanced_menu),
    UI_CONFIRM("Reset", &s_reset),
    UI_PAGE("Help", help),
    UI_EXIT("Hold to exit"),
};
static UI_MENU(s_main_menu, "Counter", s_main);

static void start(void)
{
    s_count = 0;
}

const ui_app_t ui_app_example = {
    .id = "example",
    .name = "Counter",
    .icon = LV_SYMBOL_PLUS,
    .color = 0xB28DFF,
    .start = start,
    .menu = &s_main_menu,
};

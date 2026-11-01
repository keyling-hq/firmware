/*
 * Example app, and the starting point for new ones: a counter that uses every building block of
 * the app API (ui_priv.h). Built only with CONFIG_UI_APP_EXAMPLE.
 *
 * To add an app: copy this file, give it its own id, name, icon and color, add the file to
 * CMakeLists.txt and the app to s_apps in ui.c. Screens built with the ui_menu, ui_dialog and
 * ui_text functions follow the active theme and the input rules: click moves to the next item,
 * double click goes back, hold activates.
 */
#include <stdio.h>
#include "ui_priv.h"

static void show_main(void);

static int s_count;
static int s_step = 1;

static const int s_steps[] = { 1, 5, 10 };

/* ---- items with callbacks: they run when a hold is recognised ---------------------------- */

static void add_cb(lv_event_t *e)
{
    s_count += s_step;
    ui_navigate(show_main);
}

static void step_cb(lv_event_t *e)
{
    s_step = (int)(intptr_t)lv_event_get_user_data(e);
    ui_navigate(show_main);
}

/* ---- a list with the current choice marked ----------------------------------------------- */

static void show_steps(void)
{
    ui_menu_t *menu = ui_menu_begin("Step", show_main);
    lv_obj_t *focus = NULL;
    for (size_t i = 0; i < sizeof(s_steps) / sizeof(s_steps[0]); i++) {
        char text[8];
        snprintf(text, sizeof(text), "%d", s_steps[i]);
        lv_obj_t *item = ui_menu_add(menu, text, step_cb, s_steps[i]);
        if (s_steps[i] == s_step) {
            ui_menu_mark(menu, item);
            focus = item;
        }
    }
    ui_menu_end(menu, focus);
}

/* ---- a dialog: a message and a hold to confirm ------------------------------------------- */

static void reset(void)
{
    s_count = 0;
    ui_navigate(show_main);
}

static void show_reset(void)
{
    ui_menu_t *dialog = ui_dialog_begin("Reset counter?", "Back to zero", show_main);
    lv_obj_t *cancel = ui_menu_add(dialog, "Cancel", ui_submenu_cb, (intptr_t)show_main);
    ui_hold_add(dialog, "Hold to reset", UI_HOLD_CONFIRM_MS, reset);
    ui_menu_end(dialog, cancel);
}

/* ---- a page of text ---------------------------------------------------------------------- */

static void show_help(void)
{
    ui_text_show("Help",
                 "Click moves to the next item.\n"
                 "Double click goes back.\n"
                 "Hold activates the item.\n"
                 "Hold to reset fills while held.\n"
                 "Long text like this scrolls up slowly.",
                 show_main);
}

/* ---- the root screen: back (double click) leaves the app --------------------------------- */

static void show_main(void)
{
    char count[16], step[8];
    snprintf(count, sizeof(count), "%d", s_count);
    snprintf(step, sizeof(step), "%d", s_step);
    ui_menu_t *menu = ui_menu_begin("Counter", ui_app_exit);
    lv_obj_t *add = ui_menu_add_value(menu, "Add", count, add_cb, 0);
    ui_menu_add_value(menu, "Step", step, ui_submenu_cb, (intptr_t)show_steps);
    ui_menu_add(menu, "Reset", ui_submenu_cb, (intptr_t)show_reset);
    ui_menu_add(menu, "Help", ui_submenu_cb, (intptr_t)show_help);
    ui_hold_add(menu, "Hold to exit", UI_HOLD_EXIT_MS, ui_app_exit);
    ui_menu_end(menu, add);
}

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
    .show = show_main,
};

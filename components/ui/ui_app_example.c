/*
 * Template app: every kind of menu option, doing nothing but logging. Copy it to start a new app.
 * Built only with CONFIG_UI_APP_EXAMPLE (Kconfig, "Keyling UI" → "Apps").
 *
 * To add an app: copy this file, give the ui_app_t at the bottom its own id, name, icon and color,
 * add a Kconfig option and the file to CMakeLists.txt, and the app to s_apps in ui.c. The menu
 * framework (ui_options.h) builds the screens in the active theme and runs the button: click moves
 * to the next option, hold activates it, double click goes back (and leaves the app from its first
 * menu).
 */
#include <stdio.h>
#include "ui_priv.h"
#include "esp_log.h"

static const char *TAG = "template";

/* State the options show. A real app keeps its own. */
static size_t s_color;
static bool s_extra;
static int s_runs;

/* ---- actions: run when the option is held -------------------------------------------------- */

static void action(intptr_t arg)
{
    s_runs++;
    ESP_LOGI(TAG, "action %d", (int)arg);
}

/* Shown beside the option; read again each time the menu is built, e.g. after the action. */
static const char *runs_value(intptr_t arg)
{
    static char text[12];
    snprintf(text, sizeof(text), "%d", s_runs);
    return text;
}

/* ---- a choice: the option shows the current name, holding it opens the list --------------- */

static const char *const s_colors[] = { "Red", "Green", "Blue" };

static size_t color_count(void)
{
    return sizeof(s_colors) / sizeof(s_colors[0]);
}

static const char *color_name(size_t index)
{
    return s_colors[index];
}

static size_t color_current(void)
{
    return s_color;
}

static void color_set(size_t index)
{
    s_color = index;
    ESP_LOGI(TAG, "color %s", s_colors[index]);
}

static const ui_choice_t s_color_choice = {
    .count = color_count,
    .name = color_name,
    .current = color_current,
    .set = color_set,
};

/* ---- a toggle: shows On or Off, holding flips it ----------------------------------------- */

static bool extra_get(void)
{
    return s_extra;
}

static void extra_set(bool on)
{
    s_extra = on;
}

/* shown_fn: the option is only in the menu while the toggle is on. */
static bool extra_shown(intptr_t arg)
{
    return s_extra;
}

/* ---- one table for several entries: each opens it with its own arg ----------------------- */

static const char *entry_title(intptr_t arg)
{
    return arg == 1 ? "Entry one" : "Entry two";
}

/* text_fn and marked_fn read the arg of the entry that opened this menu. */
static const char *entry_text(intptr_t arg)
{
    return ui_menu_arg() == 1 ? "First entry's action" : "Second entry's action";
}

static bool entry_marked(intptr_t arg)
{
    return ui_menu_arg() == 1;
}

static void entry_back(intptr_t arg)
{
    ui_back(1);
}

static const ui_option_t s_entry[] = {
    UI_OPTION(.kind = UI_OPT_ACTION, .text_fn = entry_text, .marked_fn = entry_marked, .fn = action, .arg = 3),
    UI_ACTION("Back (ui_back)", entry_back),
};
static UI_MENU_DYN(s_entry_menu, entry_title, s_entry);

static const ui_option_t s_nested[] = {
    UI_SUBMENU_ARG("Entry one", &s_entry_menu, 1),
    UI_SUBMENU_ARG("Entry two", &s_entry_menu, 2),
    UI_OPTION(.kind = UI_OPT_ACTION, .text = "Only while Extra is on", .shown_fn = extra_shown, .fn = action,
              .arg = 4),
};
static UI_MENU(s_nested_menu, "Nested", s_nested);

/* ---- a dialog: a menu with a message and one or two options ------------------------------ */

static const ui_option_t s_dialog[] = {
    UI_ACTION_ARG("Option one", action, 5),
    UI_ACTION_ARG("Option two", action, 6),
};
static UI_DIALOG(s_dialog_menu, "A dialog", "One or two options", s_dialog);

/* ---- confirmations: Cancel and the confirming option -------------------------------------- */

static void confirmed(intptr_t arg)
{
    ESP_LOGI(TAG, "confirmed");
}

static const ui_confirm_t s_confirm_hold = {
    .title = "Really?",
    .message = "Fills while held",
    .confirm = "Hold to confirm",
    .hold_ms = UI_HOLD_CONFIRM_MS,
    .fn = confirmed,
};

static const ui_confirm_t s_confirm_quick = {
    .title = "Really?",
    .message = "Acts once held",
    .confirm = "Confirm",
    .fn = confirmed,
};

/* ---- a page: text that scrolls up slowly when it doesn't fit ------------------------------ */

static void page(char *text, size_t len)
{
    snprintf(text, len,
             "A page of text.\n"
             "Filled when it opens, so it can show current state: %d runs so far.\n"
             "Double click goes back.",
             s_runs);
}

/* ---- the first menu ---------------------------------------------------------------------- */

static const ui_option_t s_main[] = {
    UI_OPTION(.kind = UI_OPT_ACTION, .text = "Action", .fn = action, .arg = 1, .value_fn = runs_value),
    UI_HOLD("Hold action", UI_HOLD_CONFIRM_MS, action),
    UI_CHOICE("Choice", &s_color_choice),
    UI_TOGGLE("Extra", extra_get, extra_set),
    UI_SUBMENU("Nested", &s_nested_menu),
    UI_SUBMENU("Dialog", &s_dialog_menu),
    UI_CONFIRM("Confirm (hold)", &s_confirm_hold),
    UI_CONFIRM("Confirm", &s_confirm_quick),
    UI_PAGE("Page", page),
    UI_EXIT("Hold to exit"),
};
static UI_MENU(s_main_menu, "Template", s_main);

/* Optional: runs when the app opens (stop: when it is left). */
static void start(void)
{
    s_runs = 0;
}

const ui_app_t ui_app_example = {
    .id = "template",
    .name = "Template",
    .icon = LV_SYMBOL_FILE,
    .color = 0xB28DFF,
    .start = start,
    .menu = &s_main_menu,
};

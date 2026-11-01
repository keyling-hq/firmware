#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Menus described as data: an app lists its options in const tables and the framework builds the
 * screens, runs the button rules and handles going back. Callbacks are plain C.
 *
 *     static const ui_option_t settings[] = {
 *         UI_ACTION("Type test", type_test),
 *         UI_CHOICE("Layout", &layouts),
 *         UI_SUBMENU("Advanced", &advanced_menu),
 *         UI_CONFIRM("Factory reset", &reset),
 *         UI_EXIT("Hold to exit"),
 *     };
 *     UI_MENU(settings_menu, "Settings", settings);
 *
 * Every level (a submenu, the list a choice opens, a confirmation, a page) goes on a navigation
 * stack that remembers its focused option. Double click goes up one level; at an app's root menu
 * it leaves the app. See README.md.
 */

typedef struct ui_menu_def ui_menu_def_t;

/* Callbacks receive the option's arg (0 unless set). */
typedef void (*ui_fn_t)(intptr_t arg);
typedef const char *(*ui_text_fn_t)(intptr_t arg);
typedef bool (*ui_flag_fn_t)(intptr_t arg);

/** Pick one of a list: the option shows the current name and opens the list with it marked. */
typedef struct {
    size_t (*count)(void);
    const char *(*name)(size_t index);
    size_t (*current)(void);        /* index, or count() and up for none of them */
    void (*set)(size_t index);
} ui_choice_t;

/** A confirmation: title, message, Cancel and the confirming item; returns to the menu after. */
typedef struct {
    const char *title;
    ui_text_fn_t title_fn;          /* instead of title, e.g. "Erase Main?" */
    const char *message;
    const char *confirm;            /* e.g. "Hold to erase" */
    uint32_t hold_ms;               /* 0: the item acts as soon as it is held, like other items */
    ui_fn_t fn;
} ui_confirm_t;

typedef enum {
    UI_OPT_ACTION,
    UI_OPT_HOLD,
    UI_OPT_SUBMENU,
    UI_OPT_CHOICE,
    UI_OPT_TOGGLE,
    UI_OPT_CONFIRM,
    UI_OPT_PAGE,
} ui_option_kind_t;

typedef struct {
    ui_option_kind_t kind;
    const char *text;
    intptr_t arg;
    /* Optional, evaluated whenever the screen is built: */
    ui_text_fn_t text_fn;           /* instead of text */
    ui_text_fn_t value_fn;          /* shown beside the text */
    ui_flag_fn_t marked_fn;         /* the current choice */
    ui_flag_fn_t shown_fn;          /* false hides the option */
    /* Depending on kind: */
    ui_fn_t fn;                     /* ACTION, HOLD */
    uint32_t hold_ms;               /* HOLD */
    const ui_menu_def_t *submenu;   /* SUBMENU; arg becomes ui_menu_arg() there */
    const ui_choice_t *choice;      /* CHOICE */
    bool (*get)(void);              /* TOGGLE */
    void (*set)(bool on);           /* TOGGLE */
    const ui_confirm_t *confirm;    /* CONFIRM */
    void (*page)(char *text, size_t len);   /* PAGE: fills the text */
} ui_option_t;

struct ui_menu_def {
    const char *title;
    ui_text_fn_t title_fn;          /* instead of title; gets ui_menu_arg() */
    const char *message;            /* set: shown as a dialog (a message and one or two options) */
    const ui_option_t *options;
    size_t count;
    bool exit_by_hold;              /* as an app's root: double click turns the screen off instead of
                                       leaving, so only a UI_EXIT option leaves (e.g. a running jiggler) */
};

#define UI_ACTION(text_, fn_)               { .kind = UI_OPT_ACTION, .text = (text_), .fn = (fn_) }
#define UI_ACTION_ARG(text_, fn_, arg_)     { .kind = UI_OPT_ACTION, .text = (text_), .fn = (fn_), .arg = (arg_) }
#define UI_HOLD(text_, ms_, fn_)            { .kind = UI_OPT_HOLD, .text = (text_), .hold_ms = (ms_), .fn = (fn_) }
#define UI_SUBMENU(text_, menu_)            { .kind = UI_OPT_SUBMENU, .text = (text_), .submenu = (menu_) }
#define UI_SUBMENU_ARG(text_, menu_, arg_)  { .kind = UI_OPT_SUBMENU, .text = (text_), .submenu = (menu_), .arg = (arg_) }
#define UI_CHOICE(text_, choice_)           { .kind = UI_OPT_CHOICE, .text = (text_), .choice = (choice_) }
#define UI_TOGGLE(text_, get_, set_)        { .kind = UI_OPT_TOGGLE, .text = (text_), .get = (get_), .set = (set_) }
#define UI_CONFIRM(text_, confirm_)         { .kind = UI_OPT_CONFIRM, .text = (text_), .confirm = (confirm_) }
#define UI_PAGE(text_, page_)               { .kind = UI_OPT_PAGE, .text = (text_), .page = (page_) }
/** Leaves the app; held for UI_HOLD_EXIT_MS so it isn't triggered by accident. */
#define UI_EXIT(text_)                      UI_HOLD(text_, UI_HOLD_EXIT_MS, ui_exit_fn)
/** Any combination of fields, e.g. UI_OPTION(.kind = UI_OPT_ACTION, .text_fn = f, .fn = g). */
#define UI_OPTION(...)                      { __VA_ARGS__ }

#define UI_MENU(name_, title_, options_) \
    const ui_menu_def_t name_ = { .title = (title_), .options = (options_), \
                                  .count = sizeof(options_) / sizeof((options_)[0]) }
#define UI_MENU_DYN(name_, title_fn_, options_) \
    const ui_menu_def_t name_ = { .title_fn = (title_fn_), .options = (options_), \
                                  .count = sizeof(options_) / sizeof((options_)[0]) }
#define UI_DIALOG(name_, title_, message_, options_) \
    const ui_menu_def_t name_ = { .title = (title_), .message = (message_), .options = (options_), \
                                  .count = sizeof(options_) / sizeof((options_)[0]) }

/** The arg of the submenu option that opened the current menu (0 at an app's root). */
intptr_t ui_menu_arg(void);

/** Rebuilds the current screen, keeping its focus; e.g. after state shown in it changed. */
void ui_refresh(void);

/** Goes up levels levels (as many double clicks); past the app's root menu, leaves the app. */
void ui_back(int levels);

/** For UI_EXIT and other ui_fn_t uses: leaves the app. */
void ui_exit_fn(intptr_t arg);

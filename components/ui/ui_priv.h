#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"
#include "ui.h"
#include "ui_options.h"

/*
 * The app API. Apps normally describe their menus as option tables (ui_options.h) and point
 * ui_app_t.menu at the first; the functions further down build custom screens. Either way screens
 * follow the active theme and the UI's input rules, so apps need no LVGL drawing of their own.
 * Everything here runs in the LVGL task or with the LVGL lock held. See README.md.
 */

typedef void (*ui_screen_fn_t)(void);

/*
 * An app owns the screen and the button while it is the current one. Only one app runs at a
 * time: leaving it stops it, the screen turning off does not.
 */
typedef struct {
    const char *id;             /* stable, reported to the phone */
    const char *name;
    const char *icon;           /* LV_SYMBOL_* */
    uint32_t color;             /* 0xRRGGBB, used by themes that color apps */
    void (*start)(void);        /* optional: the app became current */
    const ui_menu_def_t *menu;  /* the app's root menu (ui_options.h); or, for custom screens: */
    ui_screen_fn_t show;        /* builds the app's root screen; also used after overlays */
    void (*stop)(void);         /* optional: the app is being left */
    ui_screen_fn_t refresh;     /* optional with show: rebuilds the current screen after shown state
                                   changed; menus rebuild by themselves */
} ui_app_t;

extern const ui_app_t ui_app_jiggler;
extern const ui_app_t ui_app_settings;
extern const ui_app_t ui_app_profiles;
#if CONFIG_UI_APP_EXAMPLE
extern const ui_app_t ui_app_example;
#endif

/* Switch screens after the current event completes; only the latest request runs. */
void ui_navigate(ui_screen_fn_t fn);

void ui_emit(ui_action_t action, int arg);

/** Leaves the current app (if any) and returns to the launcher. */
void ui_app_exit(void);

const char *ui_fido_status(void);

const char *ui_profile_name(ui_profile_t profile);
ui_profile_t ui_profile_active(void);
ui_profile_t ui_profile_default(void);
ui_phantom_state_t ui_phantom_state(void);

/** The phone is typing: apps must not produce host input meanwhile. */
bool ui_host_busy(void);

bool ui_usb_mounted(void);
ui_ble_state_t ui_ble_state(void);

/** Name of the active theme, and switching to theme index (saved in settings). */
const char *ui_theme_name(size_t index);
size_t ui_theme_index(void);
void ui_theme_select(size_t index);

/*
 * Custom screens (the menu framework builds its menus with these too).
 *
 * Menus: a title and a list of items. Click moves to the next item, double click runs back
 * (NULL turns the screen off), hold activates the focused item. Lists and dialogs loop: after the
 * last item comes the first.
 *
 * A dialog is a menu with a message and one or two items (e.g. Cancel and "Hold to erase").
 *
 * Build with begin, add items, then end (which shows the screen).
 */
typedef struct ui_menu ui_menu_t;

ui_menu_t *ui_menu_begin(const char *title, ui_screen_fn_t back);
ui_menu_t *ui_dialog_begin(const char *title, const char *message, ui_screen_fn_t back);

/* Item callbacks run on LV_EVENT_PRESSED: as soon as a hold is recognised. NULL: not selectable. */
lv_obj_t *ui_menu_add(ui_menu_t *menu, const char *text, lv_event_cb_t cb, intptr_t arg);
/** An item with a current value shown beside it, e.g. "Layout" "US". */
lv_obj_t *ui_menu_add_value(ui_menu_t *menu, const char *text, const char *value, lv_event_cb_t cb, intptr_t arg);

#define UI_HOLD_EXIT_MS     1000
#define UI_HOLD_RESET_MS    3000
#define UI_HOLD_CONFIRM_MS  700

/**
 * An item that fills up while held and runs cb once held for duration_ms (counted from when the
 * hold is recognised). Releasing early resets it.
 */
lv_obj_t *ui_hold_add(ui_menu_t *menu, const char *text, uint32_t duration_ms, ui_screen_fn_t cb);

/** Marks an item as the current choice (e.g. the profile in use). */
void ui_menu_mark(ui_menu_t *menu, lv_obj_t *item);

/** Shows the screen; focus may be NULL for the first item. */
void ui_menu_end(ui_menu_t *menu, lv_obj_t *focus);

/** LV_EVENT_PRESSED handler navigating to the ui_screen_fn_t passed as user data. */
void ui_submenu_cb(lv_event_t *e);

/** A page of text that scrolls up slowly when it doesn't fit; double click runs back. */
void ui_text_show(const char *title, const char *text, ui_screen_fn_t back);

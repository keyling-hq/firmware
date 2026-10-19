#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"
#include "ui.h"

/* Everything here runs in the LVGL task or with the LVGL lock held. */

typedef void (*ui_screen_fn_t)(void);

/*
 * An app owns the screen and the button while it is the current one. Only one app runs at a
 * time: leaving it stops it, the screen turning off does not.
 */
typedef struct {
    const char *name;
    const char *icon;           /* LV_SYMBOL_*, drawn in the large font */
    void (*start)(void);        /* optional: the app became current */
    ui_screen_fn_t show;        /* builds the app's root screen; also used after overlays */
    void (*stop)(void);         /* optional: the app is being left */
} ui_app_t;

extern const ui_app_t ui_app_jiggler;
extern const ui_app_t ui_app_settings;

/* Switch screens after the current event completes; only the latest request runs. */
void ui_navigate(ui_screen_fn_t fn);

void ui_emit(ui_action_t action, int arg);

/** Leaves the current app (if any) and returns to the launcher. */
void ui_app_exit(void);

const char *ui_fido_status(void);

/** The phone is typing: apps must not produce host input meanwhile. */
bool ui_host_busy(void);

/*
 * Menu screens. back runs on double click (LV_KEY_ESC); NULL turns the screen off.
 * Items are added to the returned list container; ui_menu_end() shows the screen.
 */
lv_obj_t *ui_menu_begin(const char *title, ui_screen_fn_t back);
lv_obj_t *ui_menu_add(lv_obj_t *list, const char *text, lv_event_cb_t cb, intptr_t arg);
void ui_menu_end(lv_obj_t *list, lv_obj_t *focus);

/* Menu item callbacks run on LV_EVENT_PRESSED: as soon as a hold is recognised. */

/** LV_EVENT_PRESSED handler navigating to the ui_screen_fn_t passed as user data. */
void ui_submenu_cb(lv_event_t *e);

#define UI_HOLD_EXIT_MS     1000
#define UI_HOLD_RESET_MS    3000

/**
 * A menu item that fills up while held and runs cb once held for duration_ms (counted from when
 * the hold is recognised). Releasing early resets it.
 */
lv_obj_t *ui_hold_add(lv_obj_t *list, const char *text, uint32_t duration_ms, ui_screen_fn_t cb);

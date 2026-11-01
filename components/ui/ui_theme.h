#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"
#include "ui_priv.h"

/*
 * The theme API. The shared code (ui.c) owns navigation, focus, input and the data of each
 * screen; a theme draws that data and positions it. Positions are continuous: the launcher's pos
 * and a menu's pos move towards the focused index with an animation, and the theme's place
 * function runs for every frame. See README.md and ui_theme_example.c.
 */

#define UI_W 160
#define UI_H 80

#define UI_MENU_MAX_ITEMS   12
#define UI_ITEM_TEXT_LEN    28
#define UI_ITEM_VALUE_LEN   20
#define UI_THEME_PARTS      6
#define UI_LAUNCHER_PARTS   16

typedef enum {
    UI_MENU_LIST,       /* title and items */
    UI_MENU_DIALOG,     /* title, message, one or two items */
    UI_MENU_PROMPT,     /* security key request: title, subtitle (the site), one hold item */
    UI_MENU_RESULT,     /* title and big text, no items */
} ui_menu_kind_t;

typedef enum {
    UI_TONE_NONE,
    UI_TONE_GOOD,
    UI_TONE_BAD,
    UI_TONE_WARN,
} ui_tone_t;

typedef struct {
    lv_obj_t *obj;                  /* focusable; the theme draws inside it and positions it */
    char text[UI_ITEM_TEXT_LEN];
    char value[UI_ITEM_VALUE_LEN];
    bool marked;                    /* the current choice */
    bool hold;                      /* fills while held (hold_progress) */
    lv_obj_t *part[UI_THEME_PARTS]; /* the theme's own objects */
} ui_item_t;

struct ui_menu {
    ui_menu_kind_t kind;
    lv_obj_t *scr;
    const ui_app_t *app;            /* the running app (color, icon); NULL on overlays */
    char title[40];
    char message[64];
    char subtitle[64];
    char big[24];                   /* plain text; a theme may add ui_tone_symbol() */
    ui_tone_t tone;
    int count;
    ui_item_t items[UI_MENU_MAX_ITEMS];
    int focus;
    float pos;                      /* towards focus; unwrapped, use ui_menu_offset() */
    float focus_target;             /* where pos is heading, unwrapped */
    lv_obj_t *big_label;            /* set by the theme where it shows big, updated in place */
    lv_obj_t *part[UI_THEME_PARTS];
    ui_screen_fn_t fn;              /* the screen function that built it, for focus memory */
};

typedef struct {
    const char *profile;
    bool locked;
    ui_ble_state_t ble;
    bool usb;
} ui_status_t;

typedef struct {
    lv_obj_t *scr;
    const ui_app_t *const *apps;
    int count;
    float pos;                      /* towards the focused app; unwrapped, use ui_wrap() */
    ui_status_t status;
    lv_obj_t *part[UI_LAUNCHER_PARTS];
} ui_launcher_t;

typedef struct {
    uint32_t bg;            /* screen */
    uint32_t surface;       /* cards, unfocused items */
    uint32_t text;
    uint32_t dim;           /* secondary text */
    uint32_t accent;        /* focus, progress */
    uint32_t on_accent;     /* text on accent */
    uint32_t good, bad, warn;
} ui_palette_t;

typedef struct {
    const char *id;         /* stored in settings; stable */
    const char *name;
    ui_palette_t pal;

    /* Launcher: build creates the objects for every app and the status; place positions them. */
    void (*launcher_build)(ui_launcher_t *l);
    void (*launcher_place)(ui_launcher_t *l);
    void (*status_update)(ui_launcher_t *l);

    /* Menus of every kind: build draws the header and each item's content, place positions items. */
    void (*menu_build)(ui_menu_t *m);
    void (*menu_place)(ui_menu_t *m);
    void (*hold_progress)(ui_menu_t *m, ui_item_t *item, int percent);

    /* A page of text (ui_text_show): typically a header and ui_ticker(). */
    void (*text_build)(lv_obj_t *scr, const ui_app_t *app, const char *title, const char *text);
} ui_theme_t;

/* The built-in themes, in Settings order; the first is the default. */
extern const ui_theme_t ui_theme_candy;
extern const ui_theme_t ui_theme_device;
#if CONFIG_UI_THEME_EXAMPLE
extern const ui_theme_t ui_theme_example;
#endif

const ui_theme_t *ui_theme(void);
const ui_theme_t *ui_theme_at(size_t index);

/* ---- helpers for themes ------------------------------------------------------------------ */

/** x wrapped into [-n/2, n/2): the signed distance of an item from a looping position. */
float ui_wrap(float x, int n);
/** Distance of item i from the menu's position: wrapped when the list loops. */
float ui_menu_offset(const ui_menu_t *m, int i);
bool ui_menu_loops(const ui_menu_t *m);
/** Color a at t = 0, b at t = 1. */
lv_color_t ui_mix(uint32_t a, uint32_t b, float t);
uint32_t ui_tone_color(ui_tone_t tone);
/** LV_SYMBOL_* for a tone (in the Montserrat fonts), "" for none. */
const char *ui_tone_symbol(ui_tone_t tone);

/** A plain object without the default theme's styles. */
lv_obj_t *ui_box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t bg, int radius);
lv_obj_t *ui_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text);
/**
 * A one-line label of the given width. Text that doesn't fit scrolls in a loop, from the start and
 * after a short pause, while the label is active: labels inside items are active while their item
 * is focused (the shared code switches them), others from creation. Color and font apply to the
 * returned object.
 */
lv_obj_t *ui_label_scroll(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text, int width);
/** Where a scrolling label's text sits when it fits (left by default). */
void ui_label_scroll_align(lv_obj_t *label, lv_text_align_t align);
/** Starts (from the beginning) or stops every scrolling label in tree, e.g. a launcher page. */
void ui_scroll_activate(lv_obj_t *tree, bool active);
/** Scrolling speed in pixels per second. */
int ui_scroll_speed(void);
void ui_scroll_speed_set(int px_per_s);
/** A small down arrow that bobs: more items follow (dialogs, where only one item shows). */
lv_obj_t *ui_more_hint(lv_obj_t *parent, uint32_t color);
/** Text that scrolls up slowly in an endless loop inside the given area when it doesn't fit. */
lv_obj_t *ui_ticker(lv_obj_t *parent, int x, int y, int w, int h, const lv_font_t *font, uint32_t color,
                    const char *text);
/** Colors for the Bluetooth and USB status symbols: good when connected, dim when not. */
uint32_t ui_ble_color(const ui_status_t *s);
uint32_t ui_usb_color(const ui_status_t *s);

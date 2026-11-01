/*
 * Design concepts: small interactive prototypes drawn with plain LVGL, independent of
 * components/ui. Every concept draws the same state (launcher, an app's menu, the security key
 * prompt and its result), so they can be switched while using them.
 */
#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "bsp/tdongle.h"
#include "sim.h"

#define W 160
#define H 80
#define HOLD_APPROVE_MS 700
#define DONE_MS 1500

typedef enum { VIEW_LAUNCHER, VIEW_MENU, VIEW_FIDO, VIEW_DONE } view_t;

typedef struct {
    view_t view;
    view_t before_fido;
    int app;
    int item;
    int progress;   /* 0..100 while the approve button is held */
} state_t;

typedef struct {
    const char *icon, *name;
    uint32_t color;
    const char *items[5][2];
    int item_count;
} app_t;

static const app_t s_apps[] = {
    { LV_SYMBOL_SHUFFLE, "Jiggler", 0xFF6B6B, { { "Start", "" }, { "Interval", "30 s" }, { "Exit", "" } }, 3 },
    { LV_SYMBOL_LIST, "Profiles", 0x4ECDC4, { { "Main", "in use" }, { "Phantom", "locked" }, { "Exit", "" } }, 3 },
    { LV_SYMBOL_SETTINGS, "Settings", 0xFFD166,
      { { "Type test", "" }, { "Layout", "US" }, { "Speed", "Normal" }, { "Brightness", "80%" }, { "About", "" } }, 5 },
};
#define APP_COUNT 3

static const char *const s_site = "github.com";

/* ---- drawing helpers -------------------------------------------------------------------- */

static lv_color_t hex(uint32_t c)
{
    return lv_color_hex(c);
}

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, hex(color), 0);
    lv_label_set_text(l, text);
    return l;
}

/* A label that scrolls back and forth when it doesn't fit width. */
static lv_obj_t *label_fit(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text, int width)
{
    lv_obj_t *l = label(parent, font, color, text);
    lv_obj_set_width(l, width);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_SCROLL);
    return l;
}

static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t bg, int radius)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, hex(bg), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static void frame(lv_obj_t *o, uint32_t color, int width)
{
    lv_obj_set_style_border_color(o, hex(color), 0);
    lv_obj_set_style_border_width(o, width, 0);
}

/* First visible row so that the focused one is shown, for a window of rows. */
static int first_row(int item, int count, int rows)
{
    int first = item - rows / 2;
    if (first > count - rows) {
        first = count - rows;
    }
    return first < 0 ? 0 : first;
}

/* ---- A: Quiet. Black, light text, one amber accent for focus and progress. -------------- */

#define A_BG     0x000000
#define A_TEXT   0xEDEDED
#define A_DIM    0x6E6E6E
#define A_LINE   0x262626
#define A_ACCENT 0xFFB000

static void a_status(lv_obj_t *scr, const char *title)
{
    lv_obj_align(label(scr, &lv_font_montserrat_10, A_DIM, title), LV_ALIGN_TOP_LEFT, 6, 4);
    lv_obj_align(label(scr, &lv_font_montserrat_10, A_TEXT, LV_SYMBOL_BLUETOOTH "  " LV_SYMBOL_USB),
                 LV_ALIGN_TOP_RIGHT, -6, 4);
}

static void a_draw(lv_obj_t *scr, const state_t *s)
{
    const app_t *app = &s_apps[s->app];
    switch (s->view) {
    case VIEW_LAUNCHER:
        a_status(scr, "MAIN");
        lv_obj_align(label(scr, &lv_font_montserrat_24, A_TEXT, app->icon), LV_ALIGN_LEFT_MID, 20, 2);
        lv_obj_align(label_fit(scr, &lv_font_montserrat_20, A_TEXT, app->name, W - 64), LV_ALIGN_LEFT_MID, 58, 2);
        for (int i = 0; i < APP_COUNT; i++) {
            int x = W / 2 - (APP_COUNT * 8) / 2 + i * 8 + 2;
            box(scr, x, H - 8, i == s->app ? 6 : 3, 3, i == s->app ? A_ACCENT : A_DIM, 2);
        }
        break;
    case VIEW_MENU: {
        char title[24];
        snprintf(title, sizeof(title), "%s", app->name);
        for (char *c = title; *c; c++) {
            *c = (char)(*c >= 'a' && *c <= 'z' ? *c - 32 : *c);
        }
        lv_obj_align(label(scr, &lv_font_montserrat_10, A_DIM, title), LV_ALIGN_TOP_LEFT, 8, 3);
        box(scr, 8, 16, W - 16, 1, A_LINE, 0);
        int first = first_row(s->item, app->item_count, 3);
        for (int r = 0; r < 3 && first + r < app->item_count; r++) {
            int i = first + r, y = 21 + r * 19;
            bool focus = i == s->item;
            if (focus) {
                box(scr, 4, y, 2, 15, A_ACCENT, 1);
            }
            lv_obj_set_pos(label(scr, &lv_font_montserrat_14, focus ? A_ACCENT : A_TEXT, app->items[i][0]), 10, y);
            lv_obj_align(label(scr, &lv_font_montserrat_12, A_DIM, app->items[i][1]), LV_ALIGN_TOP_RIGHT, -8, y + 2);
        }
        break;
    }
    case VIEW_FIDO:
        lv_obj_align(label(scr, &lv_font_montserrat_10, A_DIM, LV_SYMBOL_EYE_OPEN "  SIGN IN TO"), LV_ALIGN_TOP_LEFT, 8, 6);
        lv_obj_align(label_fit(scr, &lv_font_montserrat_20, A_TEXT, s_site, W - 16), LV_ALIGN_TOP_LEFT, 8, 22);
        box(scr, 8, 58, W - 16, 3, A_LINE, 2);
        if (s->progress > 0) {
            box(scr, 8, 58, (W - 16) * s->progress / 100, 3, A_ACCENT, 2);
        }
        lv_obj_align(label(scr, &lv_font_montserrat_10, A_DIM, "Hold to approve"), LV_ALIGN_TOP_LEFT, 8, 65);
        break;
    case VIEW_DONE:
        lv_obj_align(label(scr, &lv_font_montserrat_24, A_ACCENT, LV_SYMBOL_OK), LV_ALIGN_LEFT_MID, 20, 0);
        lv_obj_align(label(scr, &lv_font_montserrat_20, A_TEXT, "Approved"), LV_ALIGN_LEFT_MID, 54, 0);
        break;
    }
}

/* ---- B: Candy. Dark navy, each app has its own color, rounded tiles. -------------------- */

#define B_BG    0x15172B
#define B_CARD  0x24274A
#define B_TEXT  0xFFFFFF
#define B_DIM   0x8A8FB8
#define B_GOOD  0x7CF29A

static void b_pills(lv_obj_t *scr)
{
    lv_obj_t *p = box(scr, W - 46, 4, 42, 13, B_CARD, 7);
    lv_obj_align(label(p, &lv_font_montserrat_10, B_GOOD, LV_SYMBOL_BLUETOOTH " " LV_SYMBOL_USB), LV_ALIGN_CENTER, 0, 0);
    lv_obj_t *m = box(scr, 4, 4, 36, 13, B_CARD, 7);
    lv_obj_align(label(m, &lv_font_montserrat_10, B_DIM, "Main"), LV_ALIGN_CENTER, 0, 0);
}

static void b_draw(lv_obj_t *scr, const state_t *s)
{
    const app_t *app = &s_apps[s->app];
    switch (s->view) {
    case VIEW_LAUNCHER: {
        b_pills(scr);
        int prev = (s->app + APP_COUNT - 1) % APP_COUNT, next = (s->app + 1) % APP_COUNT;
        lv_obj_set_style_bg_opa(box(scr, -14, 32, 28, 28, s_apps[prev].color, 8), LV_OPA_40, 0);
        lv_obj_set_style_bg_opa(box(scr, W - 14, 32, 28, 28, s_apps[next].color, 8), LV_OPA_40, 0);
        lv_obj_t *t = box(scr, 22, 22, 44, 44, app->color, 12);
        lv_obj_align(label(t, &lv_font_montserrat_24, B_BG, app->icon), LV_ALIGN_CENTER, 0, 0);
        lv_obj_align(label_fit(scr, &lv_font_montserrat_16, B_TEXT, app->name, W - 90), LV_ALIGN_TOP_LEFT, 74, 30);
        lv_obj_align(label(scr, &lv_font_montserrat_10, B_DIM, "hold to open"), LV_ALIGN_TOP_LEFT, 74, 50);
        break;
    }
    case VIEW_MENU: {
        lv_obj_t *h = box(scr, 0, 0, W, 16, app->color, 0);
        char title[32];
        snprintf(title, sizeof(title), "%s %s", app->icon, app->name);
        lv_obj_align(label(h, &lv_font_montserrat_12, B_BG, title), LV_ALIGN_LEFT_MID, 6, 0);
        int first = first_row(s->item, app->item_count, 2);
        for (int r = 0; r < 2 && first + r < app->item_count; r++) {
            int i = first + r;
            bool focus = i == s->item;
            lv_obj_t *c = box(scr, 6, 21 + r * 28, W - 12, 24, focus ? app->color : B_CARD, 8);
            lv_obj_align(label(c, &lv_font_montserrat_14, focus ? B_BG : B_TEXT, app->items[i][0]), LV_ALIGN_LEFT_MID, 8, 0);
            lv_obj_align(label(c, &lv_font_montserrat_12, focus ? B_BG : B_DIM, app->items[i][1]), LV_ALIGN_RIGHT_MID, -8, 0);
        }
        break;
    }
    case VIEW_FIDO: {
        lv_obj_t *arc = lv_arc_create(scr);
        lv_obj_set_size(arc, 62, 62);
        lv_obj_align(arc, LV_ALIGN_LEFT_MID, 8, 0);
        lv_arc_set_bg_angles(arc, 0, 360);
        lv_arc_set_rotation(arc, 270);
        lv_arc_set_value(arc, s->progress);
        lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
        lv_obj_set_style_arc_width(arc, 5, LV_PART_MAIN);
        lv_obj_set_style_arc_width(arc, 5, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(arc, hex(B_CARD), LV_PART_MAIN);
        lv_obj_set_style_arc_color(arc, hex(B_GOOD), LV_PART_INDICATOR);
        lv_obj_align(label(arc, &lv_font_montserrat_20, B_TEXT, LV_SYMBOL_EYE_OPEN), LV_ALIGN_CENTER, 0, 0);
        lv_obj_align(label(scr, &lv_font_montserrat_10, B_DIM, "Sign in to"), LV_ALIGN_TOP_LEFT, 78, 14);
        lv_obj_align(label_fit(scr, &lv_font_montserrat_14, B_TEXT, s_site, W - 82), LV_ALIGN_TOP_LEFT, 78, 28);
        lv_obj_align(label(scr, &lv_font_montserrat_10, B_GOOD, s->progress > 0 ? "keep holding..." : "hold to approve"),
                     LV_ALIGN_TOP_LEFT, 78, 52);
        break;
    }
    case VIEW_DONE: {
        lv_obj_t *c = box(scr, 14, 14, 52, 52, B_GOOD, 26);
        lv_obj_align(label(c, &lv_font_montserrat_24, B_BG, LV_SYMBOL_OK), LV_ALIGN_CENTER, 0, 0);
        lv_obj_align(label(scr, &lv_font_montserrat_16, B_TEXT, "Approved"), LV_ALIGN_LEFT_MID, 78, 0);
        break;
    }
    }
}

/* ---- C: Device. Orange LCD look, black pixel font, frames, inverted selection. ---------- */

#define C_BG  0xFF8A1E
#define C_FG  0x000000

static void c_status(lv_obj_t *scr, const char *title)
{
    lv_obj_t *bar = box(scr, 0, 0, W, 11, C_FG, 0);
    lv_obj_align(label(bar, &lv_font_unscii_8, C_BG, title), LV_ALIGN_LEFT_MID, 3, 0);
    lv_obj_align(label(bar, &lv_font_montserrat_10, C_BG, LV_SYMBOL_BLUETOOTH " " LV_SYMBOL_USB), LV_ALIGN_RIGHT_MID, -3, 0);
}

static void c_draw(lv_obj_t *scr, const state_t *s)
{
    const app_t *app = &s_apps[s->app];
    switch (s->view) {
    case VIEW_LAUNCHER: {
        c_status(scr, "MAIN");
        lv_obj_t *b = box(scr, 8, 18, 44, 44, C_BG, 0);
        frame(b, C_FG, 2);
        lv_obj_align(label(b, &lv_font_montserrat_24, C_FG, app->icon), LV_ALIGN_CENTER, 0, 0);
        lv_obj_align(label_fit(scr, &lv_font_unscii_16, C_FG, app->name, W - 64), LV_ALIGN_TOP_LEFT, 60, 24);
        char pos[16];
        snprintf(pos, sizeof(pos), "< %d/%d >", s->app + 1, APP_COUNT);
        lv_obj_align(label(scr, &lv_font_unscii_8, C_FG, pos), LV_ALIGN_TOP_LEFT, 60, 48);
        lv_obj_align(label(scr, &lv_font_unscii_8, C_FG, "HOLD:OPEN"), LV_ALIGN_BOTTOM_RIGHT, -3, -3);
        break;
    }
    case VIEW_MENU: {
        char title[24];
        snprintf(title, sizeof(title), "%s", app->name);
        for (char *c = title; *c; c++) {
            *c = (char)(*c >= 'a' && *c <= 'z' ? *c - 32 : *c);
        }
        c_status(scr, title);
        int first = first_row(s->item, app->item_count, 4);
        for (int r = 0; r < 4 && first + r < app->item_count; r++) {
            int i = first + r, y = 14 + r * 16;
            bool focus = i == s->item;
            if (focus) {
                box(scr, 2, y, W - 10, 15, C_FG, 0);
            }
            char text[40];
            snprintf(text, sizeof(text), "%-11s%s", app->items[i][0], app->items[i][1]);
            lv_obj_set_pos(label(scr, &lv_font_unscii_8, focus ? C_BG : C_FG, text), 6, y + 4);
        }
        box(scr, W - 4, 14, 1, 64, C_FG, 0);
        int knob = 64 / app->item_count;
        box(scr, W - 5, 14 + s->item * knob, 3, knob, C_FG, 0);
        break;
    }
    case VIEW_FIDO: {
        c_status(scr, "SECURITY KEY");
        lv_obj_t *d = box(scr, 4, 15, W - 8, 61, C_BG, 0);
        frame(d, C_FG, 1);
        lv_obj_align(label(d, &lv_font_unscii_8, C_FG, "Sign in to"), LV_ALIGN_TOP_LEFT, 5, 5);
        lv_obj_align(label_fit(d, &lv_font_unscii_16, C_FG, s_site, W - 20), LV_ALIGN_TOP_LEFT, 5, 17);
        int filled = s->progress * 12 / 100;
        for (int i = 0; i < 12; i++) {
            lv_obj_t *seg = box(d, 6 + i * 12, 40, 10, 6, C_FG, 0);
            if (i >= filled) {
                lv_obj_set_style_bg_opa(seg, LV_OPA_TRANSP, 0);
                frame(seg, C_FG, 1);
            }
        }
        lv_obj_align(label(d, &lv_font_unscii_8, C_FG, "HOLD TO APPROVE"), LV_ALIGN_BOTTOM_LEFT, 5, -3);
        break;
    }
    case VIEW_DONE: {
        c_status(scr, "SECURITY KEY");
        lv_obj_t *d = box(scr, 4, 15, W - 8, 61, C_FG, 0);
        lv_obj_align(label(d, &lv_font_unscii_16, C_BG, "APPROVED"), LV_ALIGN_CENTER, 0, 0);
        break;
    }
    }
}

/* ---- concepts --------------------------------------------------------------------------- */

typedef void (*draw_fn_t)(lv_obj_t *scr, const state_t *s);

static const struct {
    const char *name;
    uint32_t bg;
    draw_fn_t draw;
} s_concepts[] = {
    { "a-quiet", A_BG, a_draw },
    { "b-candy", B_BG, b_draw },
    { "c-device", C_BG, c_draw },
};
#define CONCEPT_COUNT ((int)(sizeof(s_concepts) / sizeof(s_concepts[0])))

static void render(int concept, const state_t *s)
{
    lv_obj_t *old = lv_screen_active();
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_size(scr, W, H);
    lv_obj_set_style_bg_color(scr, hex(s_concepts[concept].bg), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    s_concepts[concept].draw(scr, s);
    lv_screen_load(scr);
    lv_obj_delete(old);
}

/* ---- contact sheets --------------------------------------------------------------------- */

#define GAP 6
#define COLS 2
#define ROWS 2

void sim_mockups_render(const char *out, uint16_t *fb, int zoom)
{
    static const state_t shots[COLS * ROWS] = {
        { .view = VIEW_LAUNCHER, .app = 0 },
        { .view = VIEW_LAUNCHER, .app = 2 },
        { .view = VIEW_MENU, .app = 2, .item = 1 },
        { .view = VIEW_FIDO, .progress = 60 },
    };
    enum { SW = COLS * W + (COLS + 1) * GAP, SH = ROWS * H + (ROWS + 1) * GAP };
    static uint16_t sheet[SW * SH];
    for (int c = 0; c < CONCEPT_COUNT; c++) {
        for (int i = 0; i < SW * SH; i++) {
            sheet[i] = 0x4208;
        }
        for (int s = 0; s < COLS * ROWS; s++) {
            render(c, &shots[s]);
            lv_refr_now(NULL);
            int x0 = GAP + (s % COLS) * (W + GAP), y0 = GAP + (s / COLS) * (H + GAP);
            for (int y = 0; y < H; y++) {
                memcpy(&sheet[(y0 + y) * SW + x0], &fb[y * W], W * sizeof(uint16_t));
            }
        }
        char path[512];
        snprintf(path, sizeof(path), "%s/%s.png", out, s_concepts[c].name);
        if (sim_png_write(path, sheet, SW, SH, zoom, false)) {
            printf("wrote %s\n", path);
        }
    }
}

/* ---- interactive ------------------------------------------------------------------------ */

static state_t s_state;
static int s_concept;
static bool s_dirty = true;
static bool s_holding;
static bool s_long;
static uint32_t s_hold_start;
static uint32_t s_done_at;
static uint32_t s_now;

/* Button gestures as components/ui reads them: click on release, double click, hold. */
static void on_click(void)
{
    switch (s_state.view) {
    case VIEW_LAUNCHER:
        s_state.app = (s_state.app + 1) % APP_COUNT;
        break;
    case VIEW_MENU:
        s_state.item = (s_state.item + 1) % s_apps[s_state.app].item_count;
        break;
    default:
        return;
    }
    s_dirty = true;
}

static void on_double(void)
{
    /* The first click of the pair already moved forward: undo it. */
    if (s_state.view == VIEW_LAUNCHER) {
        s_state.app = (s_state.app + APP_COUNT - 1) % APP_COUNT;
    } else if (s_state.view == VIEW_MENU) {
        int n = s_apps[s_state.app].item_count;
        s_state.item = (s_state.item + n - 1) % n;
        s_state.view = VIEW_LAUNCHER;
    }
    s_dirty = true;
}

static void on_hold(bool down)
{
    s_holding = down;
    if (down) {
        s_hold_start = s_now;
        if (s_state.view == VIEW_LAUNCHER) {
            s_state.view = VIEW_MENU;
            s_state.item = 0;
            s_holding = false;
        }
    } else if (s_state.view == VIEW_FIDO) {
        s_state.progress = 0;
    }
    s_dirty = true;
}

static void press_down_cb(void *btn, void *usr)
{
    s_long = false;
}

static void press_up_cb(void *btn, void *usr)
{
    if (s_long) {
        on_hold(false);
    } else if (iot_button_get_repeat(btn) == 1) {
        on_click();
    }
}

static void repeat_cb(void *btn, void *usr)
{
    if (iot_button_get_repeat(btn) == 2) {
        on_double();
    }
}

static void long_cb(void *btn, void *usr)
{
    s_long = true;
    on_hold(true);
}

void sim_mockups_start(void)
{
    button_handle_t btn = sim_button_handle();
    iot_button_register_cb(btn, BUTTON_PRESS_DOWN, NULL, press_down_cb, NULL);
    iot_button_register_cb(btn, BUTTON_PRESS_UP, NULL, press_up_cb, NULL);
    iot_button_register_cb(btn, BUTTON_PRESS_REPEAT, NULL, repeat_cb, NULL);
    iot_button_register_cb(btn, BUTTON_LONG_PRESS_START, NULL, long_cb, NULL);
    printf("space: button | 1/2/3: concept | f: security key request | esc: quit\n");
}

void sim_mockups_set_concept(int concept)
{
    if (concept >= 0 && concept < CONCEPT_COUNT && concept != s_concept) {
        s_concept = concept;
        s_dirty = true;
    }
}

void sim_mockups_fido(void)
{
    if (s_state.view != VIEW_FIDO && s_state.view != VIEW_DONE) {
        s_state.before_fido = s_state.view;
    }
    s_state.view = VIEW_FIDO;
    s_state.progress = 0;
    s_dirty = true;
}

void sim_mockups_poll(uint32_t now_ms)
{
    s_now = now_ms;
    if (s_state.view == VIEW_FIDO && s_holding) {
        int progress = (int)((now_ms - s_hold_start) * 100 / HOLD_APPROVE_MS);
        if (progress >= 100) {
            s_state.view = VIEW_DONE;
            s_holding = false;
            s_done_at = now_ms;
        } else {
            s_state.progress = progress;
        }
        s_dirty = true;
    }
    if (s_state.view == VIEW_DONE && now_ms - s_done_at >= DONE_MS) {
        s_state.view = s_state.before_fido;
        s_dirty = true;
    }
    if (s_dirty) {
        s_dirty = false;
        render(s_concept, &s_state);
    }
}

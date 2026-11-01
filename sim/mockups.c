/*
 * Design concepts: static screens drawn with plain LVGL, independent of components/ui. Each
 * direction is rendered as a contact sheet of its screens.
 */
#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "sim.h"

#define W 160
#define H 80
#define GAP 6
#define COLS 2
#define ROWS 2

typedef void (*screen_fn_t)(lv_obj_t *scr);

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

static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t bg, int radius)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, hex(bg), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
    return o;
}

static void frame(lv_obj_t *o, uint32_t color, int width)
{
    lv_obj_set_style_border_color(o, hex(color), 0);
    lv_obj_set_style_border_width(o, width, 0);
}

static lv_obj_t *screen(uint32_t bg)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_size(scr, W, H);
    lv_obj_set_style_bg_color(scr, hex(bg), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    return scr;
}

/* ---- A: Quiet. Black, light text, one amber accent for focus and progress. ---------------- */

#define A_BG     0x000000
#define A_TEXT   0xEDEDED
#define A_DIM    0x6E6E6E
#define A_ACCENT 0xFFB000

static void a_status(lv_obj_t *scr)
{
    lv_obj_align(label(scr, &lv_font_montserrat_10, A_DIM, "MAIN"), LV_ALIGN_TOP_LEFT, 6, 4);
    lv_obj_align(label(scr, &lv_font_montserrat_10, A_TEXT, LV_SYMBOL_BLUETOOTH "  " LV_SYMBOL_USB),
                 LV_ALIGN_TOP_RIGHT, -6, 4);
}

static void a_dots(lv_obj_t *scr, int count, int active)
{
    for (int i = 0; i < count; i++) {
        int x = W / 2 - (count * 8) / 2 + i * 8 + 2;
        box(scr, x, H - 8, i == active ? 6 : 3, 3, i == active ? A_ACCENT : A_DIM, 2);
    }
}

static void a_app(lv_obj_t *scr, const char *icon, const char *name, int index)
{
    a_status(scr);
    lv_obj_t *i = label(scr, &lv_font_montserrat_24, A_TEXT, icon);
    lv_obj_align(i, LV_ALIGN_LEFT_MID, 20, 2);
    lv_obj_align(label(scr, &lv_font_montserrat_20, A_TEXT, name), LV_ALIGN_LEFT_MID, 58, 2);
    a_dots(scr, 3, index);
}

static void a_home(lv_obj_t *scr)
{
    a_app(scr, LV_SYMBOL_SHUFFLE, "Jiggler", 0);
}

static void a_launcher(lv_obj_t *scr)
{
    a_app(scr, LV_SYMBOL_SETTINGS, "Settings", 2);
}

static void a_menu(lv_obj_t *scr)
{
    lv_obj_align(label(scr, &lv_font_montserrat_10, A_DIM, "SETTINGS"), LV_ALIGN_TOP_LEFT, 8, 3);
    box(scr, 8, 16, W - 16, 1, 0x262626, 0);
    const char *items[][2] = { { "Type test", "" }, { "Layout", "US" }, { "Speed", "Normal" } };
    for (int i = 0; i < 3; i++) {
        int y = 21 + i * 19;
        bool focus = i == 1;
        if (focus) {
            box(scr, 4, y, 2, 15, A_ACCENT, 1);
        }
        lv_obj_t *t = label(scr, &lv_font_montserrat_14, focus ? A_ACCENT : A_TEXT, items[i][0]);
        lv_obj_set_pos(t, 10, y);
        lv_obj_t *v = label(scr, &lv_font_montserrat_12, A_DIM, items[i][1]);
        lv_obj_align(v, LV_ALIGN_TOP_RIGHT, -8, y + 2);
    }
}

static void a_fido(lv_obj_t *scr)
{
    lv_obj_align(label(scr, &lv_font_montserrat_10, A_DIM, LV_SYMBOL_EYE_OPEN "  SIGN IN TO"), LV_ALIGN_TOP_LEFT, 8, 6);
    lv_obj_align(label(scr, &lv_font_montserrat_20, A_TEXT, "github.com"), LV_ALIGN_TOP_LEFT, 8, 22);
    box(scr, 8, 58, W - 16, 3, 0x262626, 2);
    box(scr, 8, 58, (W - 16) * 6 / 10, 3, A_ACCENT, 2);
    lv_obj_align(label(scr, &lv_font_montserrat_10, A_DIM, "Hold to approve"), LV_ALIGN_TOP_LEFT, 8, 65);
}

/* ---- B: Candy. Dark navy, each app has its own color, rounded tiles. ----------------------- */

#define B_BG    0x15172B
#define B_CARD  0x24274A
#define B_TEXT  0xFFFFFF
#define B_DIM   0x8A8FB8

static const struct {
    const char *icon, *name;
    uint32_t color;
} b_apps[] = {
    { LV_SYMBOL_SHUFFLE, "Jiggler", 0xFF6B6B },
    { LV_SYMBOL_LIST, "Profiles", 0x4ECDC4 },
    { LV_SYMBOL_SETTINGS, "Settings", 0xFFD166 },
};

static void b_pills(lv_obj_t *scr)
{
    lv_obj_t *p = box(scr, W - 46, 4, 42, 13, B_CARD, 7);
    lv_obj_align(label(p, &lv_font_montserrat_10, 0x7CF29A, LV_SYMBOL_BLUETOOTH " " LV_SYMBOL_USB), LV_ALIGN_CENTER, 0, 0);
    lv_obj_t *m = box(scr, 4, 4, 36, 13, B_CARD, 7);
    lv_obj_align(label(m, &lv_font_montserrat_10, B_DIM, "Main"), LV_ALIGN_CENTER, 0, 0);
}

static void b_carousel(lv_obj_t *scr, int index)
{
    b_pills(scr);
    int prev = (index + 2) % 3, next = (index + 1) % 3;
    lv_obj_t *l = box(scr, -14, 32, 28, 28, b_apps[prev].color, 8);
    lv_obj_set_style_bg_opa(l, LV_OPA_40, 0);
    lv_obj_t *r = box(scr, W - 14, 32, 28, 28, b_apps[next].color, 8);
    lv_obj_set_style_bg_opa(r, LV_OPA_40, 0);
    lv_obj_t *t = box(scr, 22, 22, 44, 44, b_apps[index].color, 12);
    lv_obj_align(label(t, &lv_font_montserrat_24, B_BG, b_apps[index].icon), LV_ALIGN_CENTER, 0, 0);
    lv_obj_align(label(scr, &lv_font_montserrat_16, B_TEXT, b_apps[index].name), LV_ALIGN_TOP_LEFT, 74, 30);
    lv_obj_align(label(scr, &lv_font_montserrat_10, B_DIM, "hold to open"), LV_ALIGN_TOP_LEFT, 74, 50);
}

static void b_home(lv_obj_t *scr)
{
    b_carousel(scr, 0);
}

static void b_launcher(lv_obj_t *scr)
{
    b_carousel(scr, 2);
}

static void b_menu(lv_obj_t *scr)
{
    lv_obj_t *h = box(scr, 0, 0, W, 16, 0xFFD166, 0);
    lv_obj_align(label(h, &lv_font_montserrat_12, B_BG, LV_SYMBOL_SETTINGS " Settings"), LV_ALIGN_LEFT_MID, 6, 0);
    const char *items[][2] = { { "Layout", "US" }, { "Speed", "Normal" } };
    for (int i = 0; i < 2; i++) {
        bool focus = i == 0;
        lv_obj_t *c = box(scr, 6, 21 + i * 28, W - 12, 24, focus ? 0xFFD166 : B_CARD, 8);
        lv_obj_align(label(c, &lv_font_montserrat_14, focus ? B_BG : B_TEXT, items[i][0]), LV_ALIGN_LEFT_MID, 8, 0);
        lv_obj_align(label(c, &lv_font_montserrat_12, focus ? 0x5A4A10 : B_DIM, items[i][1]), LV_ALIGN_RIGHT_MID, -8, 0);
    }
}

static void b_fido(lv_obj_t *scr)
{
    lv_obj_t *arc = lv_arc_create(scr);
    lv_obj_set_size(arc, 62, 62);
    lv_obj_align(arc, LV_ALIGN_LEFT_MID, 8, 0);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_rotation(arc, 270);
    lv_arc_set_value(arc, 60);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_set_style_arc_width(arc, 5, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 5, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, hex(B_CARD), LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, hex(0x7CF29A), LV_PART_INDICATOR);
    lv_obj_align(label(arc, &lv_font_montserrat_20, B_TEXT, LV_SYMBOL_EYE_OPEN), LV_ALIGN_CENTER, 0, 0);
    lv_obj_align(label(scr, &lv_font_montserrat_10, B_DIM, "Sign in to"), LV_ALIGN_TOP_LEFT, 78, 14);
    lv_obj_align(label(scr, &lv_font_montserrat_14, B_TEXT, "github.com"), LV_ALIGN_TOP_LEFT, 78, 28);
    lv_obj_align(label(scr, &lv_font_montserrat_10, 0x7CF29A, "keep holding..."), LV_ALIGN_TOP_LEFT, 78, 52);
}

/* ---- C: Device. Orange LCD look, black pixel font, frames, inverted selection. ------------- */

#define C_BG  0xFF8A1E
#define C_FG  0x000000

static void c_status(lv_obj_t *scr, const char *title)
{
    lv_obj_t *bar = box(scr, 0, 0, W, 11, C_FG, 0);
    lv_obj_align(label(bar, &lv_font_unscii_8, C_BG, title), LV_ALIGN_LEFT_MID, 3, 0);
    lv_obj_align(label(bar, &lv_font_montserrat_10, C_BG, LV_SYMBOL_BLUETOOTH " " LV_SYMBOL_USB), LV_ALIGN_RIGHT_MID, -3, 0);
}

static void c_app(lv_obj_t *scr, const char *icon, const char *name, int index)
{
    c_status(scr, "MAIN");
    lv_obj_t *b = box(scr, 8, 18, 44, 44, C_BG, 0);
    frame(b, C_FG, 2);
    lv_obj_align(label(b, &lv_font_montserrat_24, C_FG, icon), LV_ALIGN_CENTER, 0, 0);
    lv_obj_align(label(scr, &lv_font_unscii_16, C_FG, name), LV_ALIGN_TOP_LEFT, 60, 24);
    char pos[16];
    snprintf(pos, sizeof(pos), "< %d/3 >", index + 1);
    lv_obj_align(label(scr, &lv_font_unscii_8, C_FG, pos), LV_ALIGN_TOP_LEFT, 60, 48);
    lv_obj_align(label(scr, &lv_font_unscii_8, C_FG, "HOLD:OPEN"), LV_ALIGN_BOTTOM_RIGHT, -3, -3);
}

static void c_home(lv_obj_t *scr)
{
    c_app(scr, LV_SYMBOL_SHUFFLE, "Jiggler", 0);
}

static void c_launcher(lv_obj_t *scr)
{
    c_app(scr, LV_SYMBOL_SETTINGS, "Settings", 2);
}

static void c_menu(lv_obj_t *scr)
{
    c_status(scr, "SETTINGS");
    const char *items[] = { "Type test", "Layout   US", "Speed    Normal", "Brightness" };
    for (int i = 0; i < 4; i++) {
        int y = 14 + i * 16;
        bool focus = i == 1;
        if (focus) {
            box(scr, 2, y, W - 10, 15, C_FG, 0);
        }
        lv_obj_t *t = label(scr, &lv_font_unscii_8, focus ? C_BG : C_FG, items[i]);
        lv_obj_set_pos(t, 6, y + 4);
    }
    box(scr, W - 4, 14, 1, 64, C_FG, 0);
    box(scr, W - 5, 26, 3, 14, C_FG, 0);
}

static void c_fido(lv_obj_t *scr)
{
    c_status(scr, "SECURITY KEY");
    lv_obj_t *d = box(scr, 4, 15, W - 8, 61, C_BG, 0);
    frame(d, C_FG, 1);
    lv_obj_align(label(d, &lv_font_unscii_8, C_FG, "Sign in to"), LV_ALIGN_TOP_LEFT, 5, 5);
    lv_obj_align(label(d, &lv_font_unscii_16, C_FG, "github.com"), LV_ALIGN_TOP_LEFT, 5, 17);
    for (int i = 0; i < 12; i++) {
        lv_obj_t *seg = box(d, 6 + i * 12, 40, 10, 6, C_FG, 0);
        if (i >= 7) {
            lv_obj_set_style_bg_opa(seg, LV_OPA_TRANSP, 0);
            frame(seg, C_FG, 1);
        }
    }
    lv_obj_align(label(d, &lv_font_unscii_8, C_FG, "HOLD TO APPROVE"), LV_ALIGN_BOTTOM_LEFT, 5, -3);
}

/* ---- rendering ------------------------------------------------------------------------------ */

static const struct {
    const char *name;
    uint32_t bg;
    screen_fn_t screens[COLS * ROWS];
} s_directions[] = {
    { "a-quiet", A_BG, { a_home, a_launcher, a_menu, a_fido } },
    { "b-candy", B_BG, { b_home, b_launcher, b_menu, b_fido } },
    { "c-device", C_BG, { c_home, c_launcher, c_menu, c_fido } },
};

void sim_mockups_render(const char *out, uint16_t *fb, int zoom)
{
    enum { SW = COLS * W + (COLS + 1) * GAP, SH = ROWS * H + (ROWS + 1) * GAP };
    static uint16_t sheet[SW * SH];
    for (size_t d = 0; d < sizeof(s_directions) / sizeof(s_directions[0]); d++) {
        for (int i = 0; i < SW * SH; i++) {
            sheet[i] = 0x4208;
        }
        for (int s = 0; s < COLS * ROWS; s++) {
            lv_obj_t *old = lv_screen_active();
            lv_obj_t *scr = screen(s_directions[d].bg);
            s_directions[d].screens[s](scr);
            lv_screen_load(scr);
            lv_obj_delete(old);
            lv_refr_now(NULL);
            int x0 = GAP + (s % COLS) * (W + GAP), y0 = GAP + (s / COLS) * (H + GAP);
            for (int y = 0; y < H; y++) {
                memcpy(&sheet[(y0 + y) * SW + x0], &fb[y * W], W * sizeof(uint16_t));
            }
        }
        char path[512];
        snprintf(path, sizeof(path), "%s/%s.png", out, s_directions[d].name);
        if (sim_png_write(path, sheet, SW, SH, zoom, false)) {
            printf("wrote %s\n", path);
        }
    }
}

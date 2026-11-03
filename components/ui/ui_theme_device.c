/*
 * Device: an orange LCD look with a black pixel font, a status bar, frames and a fixed selection
 * bar that lists scroll through.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "ui_theme.h"

#define BG      0xFF8A1E
#define FG      0x000000

#define BAR_H       11
#define ROW_H       16
#define SEGMENTS    12

/* Launcher parts after the per-app panels. */
enum { L_POS = 8, L_TITLE, L_BLE, L_USB };
/* Item parts. */
enum { I_TEXT, I_VALUE, I_FILL };
/* Menu parts. */
enum { M_SELECT, M_THUMB, M_SEGS, M_HINT };

static float clampf(float x, float lo, float hi)
{
    return x < lo ? lo : x > hi ? hi : x;
}

static void upper(char *dst, size_t len, const char *src)
{
    snprintf(dst, len, "%s", src);
    for (char *c = dst; *c; c++) {
        *c = (char)(*c >= 'a' && *c <= 'z' ? *c - 32 : *c);
    }
}

static void frame(lv_obj_t *o, int width)
{
    lv_obj_set_style_border_color(o, lv_color_hex(FG), 0);
    lv_obj_set_style_border_width(o, width, 0);
}

/* The black bar at the top: a title on the left, connection symbols on the right. */
static void status_bar(lv_obj_t *scr, const char *title, lv_obj_t **ble, lv_obj_t **usb)
{
    lv_obj_t *bar = ui_box(scr, 0, 0, UI_W, BAR_H, FG, 0);
    char text[40];
    upper(text, sizeof(text), title);
    lv_obj_align(ui_label_scroll(bar, &lv_font_unscii_8, BG, text, UI_W - 40), LV_ALIGN_LEFT_MID, 3, 0);
    lv_obj_t *b = ui_label(bar, &lv_font_montserrat_10, BG, LV_SYMBOL_BLUETOOTH);
    lv_obj_align(b, LV_ALIGN_RIGHT_MID, -16, 0);
    lv_obj_t *u = ui_label(bar, &lv_font_montserrat_10, BG, LV_SYMBOL_USB);
    lv_obj_align(u, LV_ALIGN_RIGHT_MID, -3, 0);
    if (ble) {
        *ble = b;
        *usb = u;
    }
}

/* ---- launcher ---------------------------------------------------------------------------- */

/* Monochrome, so no LED colors: a symbol is full while connected and faint otherwise. */
static void status_update(ui_launcher_t *l)
{
    char title[24];
    snprintf(title, sizeof(title), "%s%s", l->status.profile, l->status.locked ? " LOCKED" : "");
    upper(title, sizeof(title), title);
    lv_label_set_text(l->part[L_TITLE], title);
    lv_obj_set_style_opa(l->part[L_BLE], l->status.ble == UI_BLE_CONNECTED ? LV_OPA_COVER : LV_OPA_30, 0);
    lv_obj_set_style_opa(l->part[L_USB], l->status.usb ? LV_OPA_COVER : LV_OPA_30, 0);
}

static void launcher_build(ui_launcher_t *l)
{
    for (int i = 0; i < l->count && i < L_POS; i++) {
        lv_obj_t *panel = ui_box(l->scr, 0, BAR_H, UI_W, UI_H - BAR_H, BG, 0);
        lv_obj_set_style_bg_opa(panel, LV_OPA_TRANSP, 0);
        lv_obj_t *box = ui_box(panel, 8, 7, 44, 44, BG, 0);
        frame(box, 2);
        lv_obj_center(ui_label(box, &lv_font_montserrat_24, FG, l->apps[i]->icon));
        lv_obj_set_pos(ui_label_scroll(panel, &lv_font_unscii_16, FG, l->apps[i]->name, UI_W - 64), 60, 13);
        l->part[i] = panel;
    }
    l->part[L_POS] = ui_label(l->scr, &lv_font_unscii_8, FG, "");
    lv_obj_set_pos(l->part[L_POS], 60, BAR_H + 37);
    lv_obj_align(ui_label(l->scr, &lv_font_unscii_8, FG, "HOLD:OPEN"), LV_ALIGN_BOTTOM_RIGHT, -3, -3);
    lv_obj_t *bar = ui_box(l->scr, 0, 0, UI_W, BAR_H, FG, 0);
    l->part[L_TITLE] = ui_label(bar, &lv_font_unscii_8, BG, "");
    lv_obj_align(l->part[L_TITLE], LV_ALIGN_LEFT_MID, 3, 0);
    l->part[L_BLE] = ui_label(bar, &lv_font_montserrat_10, BG, LV_SYMBOL_BLUETOOTH);
    lv_obj_align(l->part[L_BLE], LV_ALIGN_RIGHT_MID, -16, 0);
    l->part[L_USB] = ui_label(bar, &lv_font_montserrat_10, BG, LV_SYMBOL_USB);
    lv_obj_align(l->part[L_USB], LV_ALIGN_RIGHT_MID, -3, 0);
    status_update(l);
}

/* Panels slide sideways, a screen width apart; the name scrolls once its panel has arrived. */
static void launcher_place(ui_launcher_t *l)
{
    for (int i = 0; i < l->count && i < L_POS; i++) {
        float d = ui_wrap(i - l->pos, l->count);
        lv_obj_set_x(l->part[i], (int)lroundf(d * UI_W));
        ui_scroll_activate(l->part[i], fabsf(d) < 0.01f);
    }
    int nearest = ((int)lroundf(l->pos) % l->count + l->count) % l->count;
    lv_label_set_text_fmt(l->part[L_POS], "< %d/%d >", nearest + 1, l->count);
}

/* ---- menus ------------------------------------------------------------------------------- */

/* The selection bar: in the middle of lists, at the bottom of dialogs. */
static int select_y(const ui_menu_t *m)
{
    return m->kind == UI_MENU_DIALOG ? UI_H - ROW_H - 4 : BAR_H + (UI_H - BAR_H) / 2 - ROW_H / 2;
}

static int select_w(const ui_menu_t *m)
{
    return UI_W - (m->kind == UI_MENU_LIST ? 10 : 4);
}

static void item_build(ui_menu_t *m, ui_item_t *item)
{
    if (item->hold) {
        item->part[I_FILL] = ui_box(item->obj, 0, ROW_H - 3, 0, 2, BG, 0);
    }
    char text[UI_ITEM_TEXT_LEN + 4];
    snprintf(text, sizeof(text), "%s%s", item->marked ? "*" : "", item->text);
    int value_w = item->value[0] ? 64 : 0;
    item->part[I_TEXT] = ui_label_scroll(item->obj, &lv_font_unscii_8, FG, text, UI_W - 18 - value_w);
    lv_obj_align(item->part[I_TEXT], LV_ALIGN_LEFT_MID, 4, 0);
    if (value_w) {
        item->part[I_VALUE] = ui_label_scroll(item->obj, &lv_font_unscii_8, FG, item->value, value_w - 4);
        ui_label_scroll_align(item->part[I_VALUE], LV_TEXT_ALIGN_RIGHT);
        lv_obj_align(item->part[I_VALUE], LV_ALIGN_RIGHT_MID, -4, 0);
    }
}

static void prompt_build(ui_menu_t *m)
{
    status_bar(m->scr, "Security key", NULL, NULL);
    lv_obj_t *d = ui_box(m->scr, 4, 15, UI_W - 8, 61, BG, 0);
    frame(d, 1);
    int text_w = UI_W - 20;
    if (m->site) {
        const lv_font_t *font = strlen(m->site->badge) > 1 ? &lv_font_unscii_8 : &lv_font_unscii_16;
        lv_obj_t *mark = ui_site_mark(d, m->site, UI_SITE_ICON_SIZE, FG, BG, font);
        lv_obj_align(mark, LV_ALIGN_TOP_RIGHT, -4, 4);
        lv_obj_update_layout(mark);
        text_w -= lv_obj_get_width(mark) + 4;
    }
    lv_obj_set_pos(ui_label_scroll(d, &lv_font_unscii_8, FG, m->title, text_w), 5, 5);
    lv_obj_t *site = ui_label_scroll(d, &lv_font_unscii_16, FG, m->subtitle, text_w);
    lv_obj_set_pos(site, 5, 17);
    lv_obj_t *track = ui_box(d, 5, 36, text_w, 2, lv_color_to_u32(ui_mix(BG, FG, 0.3f)) & 0xFFFFFF, 0);
    ui_label_scroll_careful(site, ui_box(track, 0, 0, text_w, 2, FG, 0));
    lv_obj_t *segs = ui_box(d, 6, 40, SEGMENTS * 12, 6, BG, 0);
    lv_obj_set_style_bg_opa(segs, LV_OPA_TRANSP, 0);
    for (int i = 0; i < SEGMENTS; i++) {
        lv_obj_t *seg = ui_box(segs, i * 12, 0, 10, 6, BG, 0);
        frame(seg, 1);
    }
    m->part[M_SEGS] = segs;
    char hint[UI_ITEM_TEXT_LEN];
    upper(hint, sizeof(hint), m->items[0].text);
    m->part[M_HINT] = ui_label(d, &lv_font_unscii_8, FG, hint);
    lv_obj_align(m->part[M_HINT], LV_ALIGN_BOTTOM_LEFT, 5, -3);
}

static void result_build(ui_menu_t *m)
{
    status_bar(m->scr, m->title, NULL, NULL);
    lv_obj_t *d = ui_box(m->scr, 4, 15, UI_W - 8, m->message[0] ? 44 : 61, FG, 0);
    lv_obj_set_flex_flow(d, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(d, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(d, 6, 0);
    if (m->tone) {
        ui_label(d, &lv_font_montserrat_20, BG, ui_tone_symbol(m->tone));
    }
    m->big_label = ui_label_scroll(d, &lv_font_unscii_16, BG, m->big, UI_W - (m->tone ? 44 : 16));
    ui_label_scroll_fit(m->big_label);
    if (m->message[0]) {
        lv_obj_align(ui_label(m->scr, &lv_font_unscii_8, FG, m->message), LV_ALIGN_BOTTOM_LEFT, 5, -4);
    }
}

static void menu_build(ui_menu_t *m)
{
    switch (m->kind) {
    case UI_MENU_PROMPT:
        prompt_build(m);
        return;
    case UI_MENU_RESULT:
        result_build(m);
        return;
    case UI_MENU_DIALOG: {
        lv_obj_t *d = ui_box(m->scr, 4, 14, UI_W - 8, 42, BG, 0);
        frame(d, 1);
        if (m->big[0]) {
            m->big_label = ui_label(d, &lv_font_unscii_16, FG, m->big);
            lv_obj_center(m->big_label);
        } else {
            lv_obj_t *msg = ui_label(d, &lv_font_unscii_8, FG, m->message);
            lv_obj_set_width(msg, UI_W - 20);
            lv_label_set_long_mode(msg, LV_LABEL_LONG_MODE_WRAP);
            lv_obj_set_pos(msg, 5, 5);
        }
        break;
    }
    case UI_MENU_LIST:
        m->part[M_THUMB] = ui_box(m->scr, UI_W - 5, 0, 3, 1, FG, 0);
        ui_box(m->scr, UI_W - 4, BAR_H + 2, 1, UI_H - BAR_H - 4, FG, 0);
        break;
    }
    /* The selection bar sits behind the items: their text turns light while they cross it. */
    m->part[M_SELECT] = ui_box(m->scr, 2, select_y(m), select_w(m), ROW_H, FG, 0);
    lv_obj_move_to_index(m->part[M_SELECT], 0);
    if (m->kind == UI_MENU_DIALOG && m->count > 1) {
        /* Only the focused item shows: an arrow on the bar says that clicking reaches more. */
        lv_obj_align(ui_more_hint(m->part[M_SELECT], BG), LV_ALIGN_RIGHT_MID, -5, 0);
    }
    for (int i = 0; i < m->count; i++) {
        item_build(m, &m->items[i]);
    }
    char title[48];
    snprintf(title, sizeof(title), "%s", m->title);
    status_bar(m->scr, title, NULL, NULL);
}

static void menu_place(ui_menu_t *m)
{
    if (m->kind == UI_MENU_PROMPT || m->kind == UI_MENU_RESULT) {
        return;
    }
    bool dialog = m->kind == UI_MENU_DIALOG;
    int sel_y = select_y(m);
    for (int i = 0; i < m->count; i++) {
        ui_item_t *item = &m->items[i];
        float d = ui_menu_offset(m, i);
        bool inside = fabsf(d) < 0.5f;
        lv_obj_set_pos(item->obj, 2, sel_y + (int)lroundf(d * ROW_H));
        lv_obj_set_size(item->obj, select_w(m), ROW_H);
        /* Dialogs show only the focused item; the others slide out of the bar. */
        bool shown = dialog ? fabsf(d) < 1 : fabsf(d) < 2.6f;
        lv_obj_set_style_opa(item->obj, shown ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        uint32_t color = inside ? BG : FG;
        lv_obj_set_style_text_color(item->part[I_TEXT], lv_color_hex(dialog && !inside ? BG : color), 0);
        if (item->part[I_VALUE]) {
            lv_obj_set_style_text_color(item->part[I_VALUE], lv_color_hex(color), 0);
        }
    }
    if (m->part[M_THUMB]) {
        int track = UI_H - BAR_H - 4, thumb = track / m->count;
        float at = fmodf(fmodf(m->pos, m->count) + m->count, m->count);
        lv_obj_set_pos(m->part[M_THUMB], UI_W - 5, BAR_H + 2 + (int)lroundf(at * track / m->count));
        lv_obj_set_height(m->part[M_THUMB], thumb);
    }
}

static void hold_progress(ui_menu_t *m, ui_item_t *item, int percent)
{
    if (m->kind == UI_MENU_PROMPT) {
        int filled = percent * SEGMENTS / 100;
        for (int i = 0; i < SEGMENTS; i++) {
            lv_obj_t *seg = lv_obj_get_child(m->part[M_SEGS], i);
            lv_obj_set_style_bg_color(seg, lv_color_hex(i < filled ? FG : BG), 0);
        }
        return;
    }
    lv_obj_set_width(item->part[I_FILL], lv_obj_get_width(item->obj) * percent / 100);
}

static void text_build(lv_obj_t *scr, const ui_app_t *app, const char *title, const char *text)
{
    ui_ticker(scr, 4, BAR_H + 3, UI_W - 8, UI_H - BAR_H - 3, &lv_font_unscii_8, FG, text);
    status_bar(scr, title, NULL, NULL);
}

const ui_theme_t ui_theme_device = {
    .id = "device",
    .name = "Device",
    .pal = { .bg = BG, .surface = BG, .text = FG, .dim = FG, .accent = FG, .on_accent = BG,
             .good = FG, .bad = FG, .warn = FG },
    .launcher_build = launcher_build,
    .launcher_place = launcher_place,
    .status_update = status_update,
    .menu_build = menu_build,
    .menu_place = menu_place,
    .hold_progress = hold_progress,
    .text_build = text_build,
};

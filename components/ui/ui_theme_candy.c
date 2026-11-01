/*
 * Candy: dark navy, a color per app, a carousel of app tiles and lists with the focused card in
 * the middle.
 */
#include <math.h>
#include <stdio.h>
#include "ui_theme.h"

#define BG      0x15172B
#define CARD    0x24274A
#define INK    0xFFFFFF
#define DIM     0x8A8FB8
#define ACCENT  0x7C9CFF
#define GOOD    0x7CF29A
#define BAD     0xFF6B6B
#define WARN    0xFFD166

#define HEADER_H    14
#define ROW_H       20
#define ROW_STEP    22

/* Launcher parts after the tiles. */
enum { L_NAME = 8, L_PROFILE_BOX, L_PROFILE, L_BLE, L_USB };
/* Item parts. */
enum { I_FILL, I_TEXT, I_VALUE };
/* Menu parts. */
enum { M_ARC, M_HINT, M_MORE };

static float clampf(float x, float lo, float hi)
{
    return x < lo ? lo : x > hi ? hi : x;
}

static uint32_t menu_color(const ui_menu_t *m)
{
    return m->app ? m->app->color : ACCENT;
}

/* ---- launcher ---------------------------------------------------------------------------- */

static void status_update(ui_launcher_t *l)
{
    char profile[24];
    snprintf(profile, sizeof(profile), "%s%s", l->status.locked ? LV_SYMBOL_WARNING " " : "", l->status.profile);
    lv_label_set_text(l->part[L_PROFILE], profile);
    lv_obj_set_style_text_color(l->part[L_PROFILE], lv_color_hex(l->status.locked ? WARN : DIM), 0);
    lv_obj_set_style_text_color(l->part[L_BLE], lv_color_hex(ui_ble_color(&l->status)), 0);
    lv_obj_set_style_text_color(l->part[L_USB], lv_color_hex(ui_usb_color(&l->status)), 0);
}

static void launcher_build(ui_launcher_t *l)
{
    for (int i = 0; i < l->count && i < L_NAME; i++) {
        lv_obj_t *tile = ui_box(l->scr, 0, 0, 1, 1, l->apps[i]->color, 0);
        lv_obj_center(ui_label(tile, &lv_font_montserrat_24, BG, l->apps[i]->icon));
        l->part[i] = tile;
    }
    l->part[L_NAME] = ui_label(l->scr, &lv_font_montserrat_14, INK, "");
    /* As wide as its text: a locked profile adds the warning symbol. */
    lv_obj_t *pill = ui_box(l->scr, 3, 3, LV_SIZE_CONTENT, 12, CARD, 6);
    lv_obj_set_style_pad_hor(pill, 7, 0);
    lv_obj_set_flex_flow(pill, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pill, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    l->part[L_PROFILE_BOX] = pill;
    l->part[L_PROFILE] = ui_label(pill, &lv_font_montserrat_10, DIM, "");
    lv_obj_t *icons = ui_box(l->scr, UI_W - 34, 3, 31, 12, CARD, 6);
    l->part[L_BLE] = ui_label(icons, &lv_font_montserrat_10, DIM, LV_SYMBOL_BLUETOOTH);
    lv_obj_align(l->part[L_BLE], LV_ALIGN_LEFT_MID, 6, 0);
    l->part[L_USB] = ui_label(icons, &lv_font_montserrat_10, DIM, LV_SYMBOL_USB);
    lv_obj_align(l->part[L_USB], LV_ALIGN_RIGHT_MID, -5, 0);
    status_update(l);
}

/* The tile at the position is large; neighbours are smaller, darker and fade out past one step. */
static void launcher_place(ui_launcher_t *l)
{
    const int cy = 36, spacing = 58;
    for (int i = 0; i < l->count && i < L_NAME; i++) {
        float d = ui_wrap(i - l->pos, l->count);
        float off = clampf(fabsf(d), 0, 1);
        float fade = clampf((1.5f - fabsf(d)) / 0.5f, 0, 1);
        int size = (int)lroundf(42 - off * 16);
        int cx = (int)lroundf(UI_W / 2.0f + d * spacing);
        lv_obj_t *tile = l->part[i];
        lv_obj_set_pos(tile, cx - size / 2, cy - size / 2);
        lv_obj_set_size(tile, size, size);
        lv_obj_set_style_radius(tile, size / 4, 0);
        lv_obj_set_style_bg_color(tile, ui_mix(l->apps[i]->color, CARD, off * 0.6f), 0);
        lv_obj_set_style_opa(tile, (lv_opa_t)(fade * 255), 0);
        lv_obj_set_style_text_font(lv_obj_get_child(tile, 0), off < 0.5f ? &lv_font_montserrat_24 : &lv_font_montserrat_16, 0);
    }
    int nearest = ((int)lroundf(l->pos) % l->count + l->count) % l->count;
    float settle = clampf(1 - 2 * fabsf(l->pos - lroundf(l->pos)), 0, 1);
    lv_label_set_text(l->part[L_NAME], l->apps[nearest]->name);
    lv_obj_set_style_opa(l->part[L_NAME], (lv_opa_t)(settle * 255), 0);
    lv_obj_align(l->part[L_NAME], LV_ALIGN_TOP_MID, 0, 61);
}

/* ---- menus ------------------------------------------------------------------------------- */

static void header(ui_menu_t *m)
{
    lv_obj_t *h = ui_box(m->scr, 0, 0, UI_W, HEADER_H, menu_color(m), 0);
    char title[48];
    snprintf(title, sizeof(title), "%s%s%s", m->app ? m->app->icon : "", m->app ? " " : "", m->title);
    lv_obj_align(ui_label_scroll(h, &lv_font_montserrat_12, BG, title, UI_W - 10), LV_ALIGN_LEFT_MID, 5, 0);
}

static void item_build(ui_menu_t *m, ui_item_t *item)
{
    if (item->hold) {
        item->part[I_FILL] = ui_box(item->obj, 0, 0, 0, ROW_H, INK, 8);
        lv_obj_set_style_bg_opa(item->part[I_FILL], LV_OPA_30, 0);
    }
    char text[UI_ITEM_TEXT_LEN + 8];
    snprintf(text, sizeof(text), "%s%s", item->marked ? LV_SYMBOL_OK " " : "", item->text);
    int value_w = item->value[0] ? 56 : 0;
    item->part[I_TEXT] = ui_label_scroll(item->obj, &lv_font_montserrat_14, INK, text, UI_W - 40 - value_w);
    lv_obj_align(item->part[I_TEXT], LV_ALIGN_LEFT_MID, 8, 0);
    if (value_w) {
        item->part[I_VALUE] = ui_label_scroll(item->obj, &lv_font_montserrat_12, DIM, item->value, value_w);
        ui_label_scroll_align(item->part[I_VALUE], LV_TEXT_ALIGN_RIGHT);
        lv_obj_align(item->part[I_VALUE], LV_ALIGN_RIGHT_MID, -8, 0);
    }
}

static void prompt_build(ui_menu_t *m)
{
    lv_obj_t *arc = lv_arc_create(m->scr);
    lv_obj_remove_style_all(arc);
    lv_obj_set_size(arc, 62, 62);
    lv_obj_set_pos(arc, 8, 9);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_rotation(arc, 270);
    lv_arc_set_value(arc, 0);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(arc, 5, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 5, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_hex(CARD), LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(ui_tone_color(m->tone)), LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_center(ui_label(arc, &lv_font_montserrat_20, INK, m->tone == UI_TONE_BAD ? LV_SYMBOL_TRASH : LV_SYMBOL_EYE_OPEN));
    m->part[M_ARC] = arc;

    lv_obj_set_pos(ui_label_scroll(m->scr, &lv_font_montserrat_10, DIM, m->title, UI_W - 82), 78, 12);
    lv_obj_set_pos(ui_label_scroll(m->scr, &lv_font_montserrat_14, INK, m->subtitle, UI_W - 82), 78, 26);
    m->part[M_HINT] = ui_label(m->scr, &lv_font_montserrat_10, ui_tone_color(m->tone), m->items[0].text);
    lv_obj_set_pos(m->part[M_HINT], 78, 52);
}

static void result_build(ui_menu_t *m)
{
    lv_obj_set_pos(ui_label_scroll(m->scr, &lv_font_montserrat_12, DIM, m->title, UI_W - 16), 8, 8);
    /* The symbol beside the text, centred together; long text scrolls. */
    lv_obj_t *row = ui_box(m->scr, 0, 28, UI_W, 26, BG, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 6, 0);
    if (m->tone) {
        ui_label(row, &lv_font_montserrat_20, ui_tone_color(m->tone), ui_tone_symbol(m->tone));
    }
    m->big_label = ui_label_scroll(row, &lv_font_montserrat_20, ui_tone_color(m->tone), m->big, UI_W - 44);
    ui_label_scroll_fit(m->big_label);
    if (m->message[0]) {
        lv_obj_align(ui_label(m->scr, &lv_font_montserrat_10, DIM, m->message), LV_ALIGN_BOTTOM_MID, 0, -6);
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
    case UI_MENU_DIALOG:
        if (m->big[0]) {
            m->big_label = ui_label(m->scr, &lv_font_montserrat_24, INK, m->big);
            lv_obj_align(m->big_label, LV_ALIGN_TOP_MID, 0, HEADER_H + 4);
        } else if (m->message[0]) {
            lv_obj_t *msg = ui_label(m->scr, &lv_font_montserrat_12, DIM, m->message);
            lv_obj_set_width(msg, UI_W - 16);
            lv_label_set_long_mode(msg, LV_LABEL_LONG_MODE_WRAP);
            lv_obj_set_pos(msg, 8, HEADER_H + 4);
        }
        if (m->count > 1) {
            /* Only the focused item shows: an arrow on it says that clicking reaches more. */
            m->part[M_MORE] = ui_more_hint(m->scr, BG);
            lv_obj_align(m->part[M_MORE], LV_ALIGN_BOTTOM_RIGHT, -16, -9);
        }
        break;
    case UI_MENU_LIST:
        break;
    }
    for (int i = 0; i < m->count; i++) {
        item_build(m, &m->items[i]);
    }
    header(m);
}

/* Lists: the card at the position is filled with the menu's color; the others narrower and dark. */
static void menu_place(ui_menu_t *m)
{
    if (m->kind == UI_MENU_PROMPT || m->kind == UI_MENU_RESULT) {
        return;
    }
    bool dialog = m->kind == UI_MENU_DIALOG;
    const int cy = dialog ? UI_H - ROW_H / 2 - 4 : HEADER_H + (UI_H - HEADER_H) / 2 + 1;
    uint32_t color = menu_color(m);
    for (int i = 0; i < m->count; i++) {
        ui_item_t *item = &m->items[i];
        float d = ui_menu_offset(m, i);
        float off = clampf(fabsf(d), 0, 1);
        float fade = dialog ? 1 - off : clampf((1.5f - fabsf(d)) / 0.5f, 0, 1);
        int inset = 6 + (int)lroundf(off * 8);
        int y = (int)lroundf(cy + d * (dialog ? ROW_H : ROW_STEP)) - ROW_H / 2;
        lv_obj_set_pos(item->obj, inset, y);
        lv_obj_set_size(item->obj, UI_W - 2 * inset, ROW_H);
        lv_obj_set_style_radius(item->obj, 8, 0);
        lv_obj_set_style_bg_opa(item->obj, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(item->obj, ui_mix(color, CARD, off), 0);
        lv_obj_set_style_opa(item->obj, (lv_opa_t)(fade * 255), 0);
        lv_obj_set_style_text_color(item->part[I_TEXT], ui_mix(BG, INK, off), 0);
        if (item->part[I_VALUE]) {
            lv_obj_set_style_text_color(item->part[I_VALUE], ui_mix(BG, DIM, off), 0);
        }
    }
}

static void hold_progress(ui_menu_t *m, ui_item_t *item, int percent)
{
    if (m->kind == UI_MENU_PROMPT) {
        lv_arc_set_value(m->part[M_ARC], percent);
        lv_label_set_text(m->part[M_HINT], percent > 0 ? "keep holding..." : item->text);
        return;
    }
    lv_obj_set_width(item->part[I_FILL], lv_obj_get_width(item->obj) * percent / 100);
}

static void text_build(lv_obj_t *scr, const ui_app_t *app, const char *title, const char *text)
{
    ui_ticker(scr, 8, HEADER_H + 2, UI_W - 16, UI_H - HEADER_H - 2, &lv_font_montserrat_12, INK, text);
    lv_obj_t *h = ui_box(scr, 0, 0, UI_W, HEADER_H, app ? app->color : ACCENT, 0);
    lv_obj_align(ui_label(h, &lv_font_montserrat_12, BG, title), LV_ALIGN_LEFT_MID, 5, 0);
}

const ui_theme_t ui_theme_candy = {
    .id = "candy",
    .name = "Candy",
    .pal = { .bg = BG, .surface = CARD, .text = INK, .dim = DIM, .accent = ACCENT, .on_accent = BG,
             .good = GOOD, .bad = BAD, .warn = WARN },
    .launcher_build = launcher_build,
    .launcher_place = launcher_place,
    .status_update = status_update,
    .menu_build = menu_build,
    .menu_place = menu_place,
    .hold_progress = hold_progress,
    .text_build = text_build,
};

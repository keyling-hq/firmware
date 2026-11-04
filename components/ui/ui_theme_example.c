/*
 * Quiet: the example theme, and the starting point for new ones. Black, light text and one amber
 * accent; lists show three rows with the focused one marked by a bar.
 *
 * To add a theme: copy this file, change id, name and the drawing, add the file to CMakeLists.txt
 * and the theme to s_themes in ui.c. Built only with CONFIG_UI_THEME_EXAMPLE.
 *
 * What the shared code provides and calls:
 * - launcher_build/launcher_place: l->apps[i] (name, icon; ui_app_slot() to color them), l->status,
 *   l->pos. pos moves smoothly between app indexes; ui_wrap() turns it into each app's distance
 *   from the middle.
 * - menu_build/menu_place: m->kind, title, message, subtitle, big, tone, site (a known site on
 *   security key prompts, see ui_site_mark()), items (text, value, marked, hold). Each item's obj is focusable and empty: draw inside it, then position it in
 *   menu_place from ui_menu_offset(), which is the item's distance from the focused position.
 * - hold_progress: a hold item's fill, 0..100.
 * - text_build: a page of text; ui_ticker() scrolls it.
 * Objects stored in part[] belong to the theme. The screens are rebuilt when the theme changes.
 */
#include <math.h>
#include <stdio.h>
#include "ui_theme.h"

#define BG      0x000000
#define INK    0xEDEDED
#define DIM     0x6E6E6E
#define LINE    0x262626
#define ACCENT  0xFFB000

#define ROW_STEP    19

enum { L_ICON, L_NAME, L_DOTS, L_STATUS, L_BLE, L_USB };
enum { I_BAR, I_TEXT, I_VALUE, I_FILL };

static void status_update(ui_launcher_t *l)
{
    lv_label_set_text(l->part[L_STATUS], l->status.profile);
    lv_obj_set_style_text_color(l->part[L_BLE], lv_color_hex(ui_ble_color(&l->status)), 0);
    if (l->status.usb) {
        lv_obj_add_flag(l->part[L_USB], LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(l->part[L_USB], LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_align(l->part[L_BLE], LV_ALIGN_TOP_RIGHT, l->status.usb ? -6 : -22, 4);
}

static void launcher_build(ui_launcher_t *l)
{
    l->part[L_STATUS] = ui_label(l->scr, &lv_font_montserrat_10, DIM, "");
    lv_obj_set_pos(l->part[L_STATUS], 6, 4);
    l->part[L_BLE] = ui_label(l->scr, &lv_font_montserrat_10, DIM, LV_SYMBOL_BLUETOOTH);
    l->part[L_USB] = ui_usb_off_symbol(l->scr, DIM, BG);
    lv_obj_align(l->part[L_USB], LV_ALIGN_TOP_RIGHT, -6, 4);
    l->part[L_ICON] = ui_label(l->scr, &lv_font_montserrat_24, INK, "");
    l->part[L_NAME] = ui_label(l->scr, &lv_font_montserrat_20, INK, "");
    l->part[L_DOTS] = ui_box(l->scr, 0, UI_H - 8, UI_W, 3, BG, 0);
    lv_obj_set_style_bg_opa(l->part[L_DOTS], LV_OPA_TRANSP, 0);
    for (int i = 0; i < l->count; i++) {
        ui_box(l->part[L_DOTS], UI_W / 2 - l->count * 4 + i * 8, 0, 3, 3, DIM, 2);
    }
    status_update(l);
}

/* No animation of its own: shows the app nearest to the position. */
static void launcher_place(ui_launcher_t *l)
{
    int app = ((int)lroundf(l->pos) % l->count + l->count) % l->count;
    lv_label_set_text(l->part[L_ICON], l->apps[app]->icon);
    lv_obj_align(l->part[L_ICON], LV_ALIGN_LEFT_MID, 20, 2);
    lv_label_set_text(l->part[L_NAME], l->apps[app]->name);
    lv_obj_align(l->part[L_NAME], LV_ALIGN_LEFT_MID, 58, 2);
    for (int i = 0; i < l->count; i++) {
        lv_obj_t *dot = lv_obj_get_child(l->part[L_DOTS], i);
        lv_obj_set_style_bg_color(dot, lv_color_hex(i == app ? ACCENT : DIM), 0);
    }
}

static void menu_build(ui_menu_t *m)
{
    lv_obj_set_pos(ui_label_scroll(m->scr, &lv_font_montserrat_10, DIM, m->title, UI_W - 16), 8, 3);
    ui_box(m->scr, 8, 16, UI_W - 16, 1, LINE, 0);
    if (m->kind == UI_MENU_PROMPT) {
        lv_obj_set_pos(ui_label_scroll(m->scr, &lv_font_montserrat_20, INK, m->subtitle, UI_W - 16), 8, 22);
    } else if (m->big[0]) {
        lv_obj_t *row = ui_box(m->scr, 0, 30, UI_W, 26, BG, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, 6, 0);
        if (m->tone) {
            ui_label(row, &lv_font_montserrat_20, ui_tone_color(m->tone), ui_tone_symbol(m->tone));
        }
        m->big_label = ui_label_scroll(row, &lv_font_montserrat_20, ui_tone_color(m->tone), m->big, UI_W - 44);
        ui_label_scroll_fit(m->big_label);
    } else if (m->message[0]) {
        lv_obj_set_pos(ui_label_scroll(m->scr, &lv_font_montserrat_12, DIM, m->message, UI_W - 16), 8, 21);
    }
    if (m->kind == UI_MENU_DIALOG && m->count > 1) {
        lv_obj_align(ui_more_hint(m->scr, ACCENT), LV_ALIGN_BOTTOM_RIGHT, -8, -5);
    }
    for (int i = 0; i < m->count; i++) {
        ui_item_t *item = &m->items[i];
        if (item->hold) {
            item->part[I_FILL] = ui_box(item->obj, 0, 15, 0, 2, ACCENT, 0);
        }
        item->part[I_BAR] = ui_box(item->obj, 0, 0, 2, 15, ACCENT, 1);
        char text[UI_ITEM_TEXT_LEN + 8];
        snprintf(text, sizeof(text), "%s%s", item->marked ? LV_SYMBOL_OK " " : "", item->text);
        item->part[I_TEXT] = ui_label_scroll(item->obj, &lv_font_montserrat_14, INK, text, item->value[0] ? UI_W - 70 : UI_W - 24);
        lv_obj_set_pos(item->part[I_TEXT], 6, 0);
        item->part[I_VALUE] = ui_label(item->obj, &lv_font_montserrat_12, DIM, item->value);
        lv_obj_align(item->part[I_VALUE], LV_ALIGN_TOP_RIGHT, 0, 2);
    }
}

/* Lists scroll so the focused row is the middle one; dialogs show their items at the bottom. */
static void menu_place(ui_menu_t *m)
{
    bool list = m->kind == UI_MENU_LIST;
    int middle = list ? 40 : 61;
    for (int i = 0; i < m->count; i++) {
        ui_item_t *item = &m->items[i];
        float d = ui_menu_offset(m, i);
        lv_obj_set_pos(item->obj, 4, middle + (int)lroundf(d * ROW_STEP));
        lv_obj_set_size(item->obj, UI_W - 12, 17);
        bool focused = i == m->focus;
        bool shown = list ? fabsf(d) < 1.6f : fabsf(d) < 0.5f;
        lv_obj_set_style_opa(item->obj, shown ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_opa(item->part[I_BAR], focused ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(item->part[I_TEXT], lv_color_hex(focused ? ACCENT : INK), 0);
    }
}

static void hold_progress(ui_menu_t *m, ui_item_t *item, int percent)
{
    lv_obj_set_width(item->part[I_FILL], lv_obj_get_width(item->obj) * percent / 100);
}

static void text_build(lv_obj_t *scr, const ui_app_t *app, const char *title, const char *text)
{
    lv_obj_set_pos(ui_label(scr, &lv_font_montserrat_10, DIM, title), 8, 3);
    ui_box(scr, 8, 16, UI_W - 16, 1, LINE, 0);
    ui_ticker(scr, 8, 19, UI_W - 16, UI_H - 19, &lv_font_montserrat_12, INK, text);
}

const ui_theme_t ui_theme_example = {
    .id = "quiet",
    .name = "Quiet",
    .pal = { .bg = BG, .surface = LINE, .text = INK, .dim = DIM, .accent = ACCENT, .on_accent = BG,
             .good = 0x7CF29A, .bad = 0xFF6B6B, .warn = ACCENT },
    .launcher_build = launcher_build,
    .launcher_place = launcher_place,
    .status_update = status_update,
    .menu_build = menu_build,
    .menu_place = menu_place,
    .hold_progress = hold_progress,
    .text_build = text_build,
};

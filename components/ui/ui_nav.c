/*
 * The menu framework (ui_options.h): a navigation stack of levels, each built as a screen with
 * the menu API whenever it is shown. Items carry their option's index in their user data.
 */
#include <stdio.h>
#include "ui_priv.h"
#include "ui_nav.h"
#include "esp_log.h"

static const char *TAG = "ui_nav";

#define NAV_DEPTH       8
#define PAGE_TEXT_LEN   512

typedef enum {
    LEVEL_MENU,
    LEVEL_CHOICE,
    LEVEL_CONFIRM,
    LEVEL_PAGE,
} level_kind_t;

typedef struct {
    level_kind_t kind;
    const ui_menu_def_t *menu;      /* LEVEL_MENU */
    const ui_option_t *option;      /* the option that opened a choice, confirmation or page */
    intptr_t arg;
    int focus;                      /* option (or list) index */
} level_t;

static level_t s_levels[NAV_DEPTH];
static int s_depth;
/* Set when a callback navigated, so the framework doesn't also rebuild the screen. */
static bool s_moved;

static level_t *top(void)
{
    return s_depth > 0 ? &s_levels[s_depth - 1] : NULL;
}

static int item_index(lv_obj_t *item)
{
    return (int)(intptr_t)lv_obj_get_user_data(item) - 1;
}

static void tag(lv_obj_t *item, int index)
{
    lv_obj_set_user_data(item, (void *)(intptr_t)(index + 1));
}

static void show_top(void);

static void push(level_kind_t kind, const ui_menu_def_t *menu, const ui_option_t *option, intptr_t arg)
{
    if (s_depth == NAV_DEPTH) {
        ESP_LOGE(TAG, "menus nested deeper than %d", NAV_DEPTH);
        return;
    }
    s_levels[s_depth++] = (level_t){ .kind = kind, .menu = menu, .option = option, .arg = arg };
    s_moved = true;
    ui_navigate(show_top);
}

void ui_back(int levels)
{
    s_moved = true;
    s_depth -= levels;
    if (s_depth <= 0) {
        s_depth = 0;
        ui_app_exit();
        return;
    }
    ui_navigate(show_top);
}

void ui_exit_fn(intptr_t arg)
{
    s_moved = true;
    s_depth = 0;
    ui_app_exit();
}

static void back(void)
{
    ui_back(1);
}

intptr_t ui_menu_arg(void)
{
    return top() ? top()->arg : 0;
}

void ui_refresh(void)
{
    if (s_depth > 0) {
        ui_navigate(show_top);
    }
}

/* Focus moves are remembered as they happen, so a rebuild (or an overlay) keeps the position. */
static void focus_cb(lv_event_t *e)
{
    lv_obj_t *item = lv_event_get_target_obj(e);
    if (top() && lv_obj_get_screen(item) == lv_screen_active()) {
        top()->focus = item_index(item);
    }
}

static const char *option_text(const ui_option_t *o)
{
    return o->text_fn ? o->text_fn(o->arg) : o->text;
}

/* Runs a callback; when it didn't navigate, the screen is rebuilt to show what changed. */
static void run(ui_fn_t fn, intptr_t arg)
{
    s_moved = false;
    if (fn) {
        fn(arg);
    }
    if (!s_moved) {
        ui_refresh();
    }
}

/* ---- menus ------------------------------------------------------------------------------- */

static void activate(int index)
{
    level_t *level = top();
    const ui_option_t *o = &level->menu->options[index];
    level->focus = index;
    switch (o->kind) {
    case UI_OPT_ACTION:
    case UI_OPT_HOLD:
        run(o->fn, o->arg);
        break;
    case UI_OPT_SUBMENU:
        push(LEVEL_MENU, o->submenu, o, o->arg);
        break;
    case UI_OPT_CHOICE:
        push(LEVEL_CHOICE, NULL, o, level->arg);
        break;
    case UI_OPT_TOGGLE:
        o->set(!o->get());
        ui_refresh();
        break;
    case UI_OPT_CONFIRM:
        push(LEVEL_CONFIRM, NULL, o, level->arg);
        break;
    case UI_OPT_PAGE:
        push(LEVEL_PAGE, NULL, o, level->arg);
        break;
    }
}

static void item_cb(lv_event_t *e)
{
    activate(item_index(lv_event_get_target_obj(e)));
}

/* Hold items call back without an event: the item being held is the focused one. */
static void hold_cb(void)
{
    activate(item_index(lv_group_get_focused(lv_group_get_default())));
}

static const char *option_value(const ui_option_t *o)
{
    if (o->value_fn) {
        return o->value_fn(o->arg);
    }
    if (o->kind == UI_OPT_TOGGLE) {
        return o->get() ? "On" : "Off";
    }
    if (o->kind == UI_OPT_CHOICE) {
        size_t current = o->choice->current();
        return current < o->choice->count() ? o->choice->name(current) : "";
    }
    return NULL;
}

static lv_obj_t *add_item(ui_menu_t *menu, int index, const char *text, const char *value, uint32_t hold_ms,
                          lv_event_cb_t cb, ui_screen_fn_t hold)
{
    lv_obj_t *item = hold_ms ? ui_hold_add(menu, text, hold_ms, hold) : ui_menu_add_value(menu, text, value, cb, 0);
    if (item) {
        tag(item, index);
        lv_obj_add_event_cb(item, focus_cb, LV_EVENT_FOCUSED, NULL);
    }
    return item;
}

static void show_menu(level_t *level)
{
    const ui_menu_def_t *def = level->menu;
    const char *title = def->title_fn ? def->title_fn(level->arg) : def->title;
    ui_screen_fn_t on_back = s_depth == 1 && def->exit_by_hold ? NULL : back;
    ui_menu_t *menu = def->message ? ui_dialog_begin(title, def->message, on_back) : ui_menu_begin(title, on_back);
    lv_obj_t *first = NULL, *focus = NULL;
    for (size_t i = 0; i < def->count; i++) {
        const ui_option_t *o = &def->options[i];
        if (o->shown_fn && !o->shown_fn(o->arg)) {
            continue;
        }
        lv_obj_t *item = add_item(menu, (int)i, option_text(o), option_value(o),
                                  o->kind == UI_OPT_HOLD ? o->hold_ms : 0, item_cb, hold_cb);
        if (!item) {
            continue;
        }
        if (o->marked_fn && o->marked_fn(o->arg)) {
            ui_menu_mark(menu, item);
        }
        first = first ? first : item;
        if ((int)i == level->focus) {
            focus = item;
        }
    }
    /* A focused option that is hidden now falls back to the first one. */
    ui_menu_end(menu, focus ? focus : first);
}

/* ---- choices ----------------------------------------------------------------------------- */

static void choice_cb(lv_event_t *e)
{
    top()->option->choice->set((size_t)item_index(lv_event_get_target_obj(e)));
    ui_back(1);
}

static void show_choice(level_t *level, bool first)
{
    const ui_choice_t *choice = level->option->choice;
    size_t current = choice->current();
    if (first) {
        level->focus = current < choice->count() ? (int)current : 0;
    }
    ui_menu_t *menu = ui_menu_begin(option_text(level->option), back);
    lv_obj_t *focus = NULL;
    for (size_t i = 0; i < choice->count(); i++) {
        lv_obj_t *item = add_item(menu, (int)i, choice->name(i), NULL, 0, choice_cb, NULL);
        if (item && i == current) {
            ui_menu_mark(menu, item);
        }
        if ((int)i == level->focus) {
            focus = item;
        }
    }
    ui_menu_end(menu, focus);
}

/* ---- confirmations ----------------------------------------------------------------------- */

static void confirmed(void)
{
    level_t *level = top();
    s_moved = false;
    level->option->confirm->fn(level->arg);
    if (!s_moved) {
        ui_back(1);
    }
}

static void confirm_cb(lv_event_t *e)
{
    confirmed();
}

static void cancel_cb(lv_event_t *e)
{
    ui_back(1);
}

static void show_confirm(level_t *level)
{
    const ui_confirm_t *c = level->option->confirm;
    const char *title = c->title_fn ? c->title_fn(level->arg) : c->title ? c->title : option_text(level->option);
    ui_menu_t *dialog = ui_dialog_begin(title, c->message, back);
    lv_obj_t *cancel = add_item(dialog, 0, "Cancel", NULL, 0, cancel_cb, NULL);
    lv_obj_t *confirm = add_item(dialog, 1, c->confirm ? c->confirm : "Confirm", NULL, c->hold_ms, confirm_cb,
                                 confirmed);
    ui_menu_end(dialog, level->focus == 1 ? confirm : cancel);
}

/* ---- pages ------------------------------------------------------------------------------- */

static void show_page(level_t *level)
{
    static char text[PAGE_TEXT_LEN];
    text[0] = '\0';
    level->option->page(text, sizeof(text));
    ui_text_show(option_text(level->option), text, back);
}

/* ---- entry points for ui.c --------------------------------------------------------------- */

static int s_shown_depth;

static void show_top(void)
{
    level_t *level = top();
    if (!level) {
        return;
    }
    bool first = s_depth > s_shown_depth;
    s_shown_depth = s_depth;
    switch (level->kind) {
    case LEVEL_MENU:
        show_menu(level);
        break;
    case LEVEL_CHOICE:
        show_choice(level, first);
        break;
    case LEVEL_CONFIRM:
        show_confirm(level);
        break;
    case LEVEL_PAGE:
        show_page(level);
        break;
    }
}

void ui_nav_open(const ui_menu_def_t *root)
{
    s_depth = 0;
    s_shown_depth = 0;
    s_levels[s_depth++] = (level_t){ .kind = LEVEL_MENU, .menu = root };
    show_top();
}

void ui_nav_show(void)
{
    show_top();
}

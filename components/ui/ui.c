#include <stdio.h>
#include <string.h>
#include "ui.h"
#include "ui_input.h"
#include "bsp/tdongle.h"
#include "esp_idf_version.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "keymap.h"
#include "settings.h"

static const char *TAG = "ui";

#define SCREEN_TICK_MS      250
#define MENU_RESET_MS       (2 * 60 * 1000)
#define ITEM_HEIGHT         20

typedef void (*menu_fn_t)(void);

/* Screens driven by device state rather than navigation, highest priority last. */
typedef enum {
    OVERLAY_NONE,
    OVERLAY_PAIRING,
    OVERLAY_TYPING,
    OVERLAY_PASSKEY,
} overlay_t;

static void navigate(menu_fn_t fn);
static void show_main_menu(void);

static ui_action_cb_t s_action_cb;
static lv_indev_t *s_indev;
static lv_obj_t *s_status;
static menu_fn_t s_back;
static volatile bool s_screen_on = true;
static uint32_t s_screen_off_tick;
static bool s_usb_mounted;
static ui_ble_state_t s_ble_state = UI_BLE_ADVERTISING;
static bool s_typing;
static bool s_pairing;
static bool s_passkey_shown;
static uint32_t s_passkey;
static overlay_t s_overlay = OVERLAY_NONE;

static void screen_set(bool on)
{
    if (on == s_screen_on) {
        return;
    }
    s_screen_on = on;
    if (on) {
        if (lv_tick_elaps(s_screen_off_tick) >= MENU_RESET_MS && s_overlay == OVERLAY_NONE) {
            navigate(show_main_menu);
        }
        settings_t settings;
        settings_get(&settings);
        bsp_display_brightness_set(settings.brightness);
        lv_display_trigger_activity(NULL);
    } else {
        s_screen_off_tick = lv_tick_get();
        bsp_display_brightness_set(0);
    }
}

bool ui_screen_is_on(void)
{
    return s_screen_on;
}

void ui_screen_wake(void)
{
    screen_set(true);
}

static void screen_timer_cb(lv_timer_t *timer)
{
    if (!s_screen_on || s_overlay != OVERLAY_NONE) {
        return;
    }
    settings_t settings;
    settings_get(&settings);
    if (lv_display_get_inactive_time(NULL) >= settings.screen_timeout_s * 1000U) {
        screen_set(false);
    }
}

static void emit(ui_action_t action, int arg)
{
    if (s_action_cb) {
        s_action_cb(action, arg);
    }
}

/* ---- menu scaffolding ---------------------------------------------------- */

static menu_fn_t s_pending_nav;

static void navigate_async_cb(void *arg)
{
    menu_fn_t fn = s_pending_nav;
    s_pending_nav = NULL;
    if (fn) {
        fn();
    }
}

/*
 * Event handlers must not delete their own screen: switch screens after the event completes.
 * Only the latest request runs; queued async calls don't execute in submission order.
 */
static void navigate(menu_fn_t fn)
{
    if (!s_pending_nav) {
        lv_async_call(navigate_async_cb, NULL);
    }
    s_pending_nav = fn;
}

static void item_key_cb(lv_event_t *e)
{
    if (lv_event_get_key(e) != LV_KEY_ESC) {
        return;
    }
    if (s_back) {
        navigate(s_back);
    } else {
        screen_set(false);
    }
}

static lv_obj_t *menu_begin(const char *title, menu_fn_t back)
{
    s_back = back;
    s_status = NULL;

    lv_group_t *group = lv_group_create();
    lv_group_set_wrap(group, true);
    lv_group_set_default(group);

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(scr, 2, 0);
    lv_obj_set_style_pad_row(scr, 2, 0);
    lv_obj_set_user_data(scr, group);

    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, title);

    lv_obj_t *list = lv_obj_create(scr);
    lv_obj_set_width(list, lv_pct(100));
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(list, 2, 0);
    lv_obj_set_style_pad_row(list, 2, 0);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
    return list;
}

static lv_obj_t *menu_add(lv_obj_t *list, const char *text, lv_event_cb_t cb, intptr_t arg)
{
    lv_obj_t *btn = lv_button_create(list);
    lv_obj_set_size(btn, lv_pct(100), ITEM_HEIGHT);
    lv_obj_add_event_cb(btn, item_key_cb, LV_EVENT_KEY, NULL);
    if (cb) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, (void *)arg);
    }

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return btn;
}

static void menu_end(lv_obj_t *list, lv_obj_t *focus)
{
    lv_obj_t *scr = lv_obj_get_screen(list);
    lv_group_t *group = lv_obj_get_user_data(scr);
    lv_obj_t *old_scr = lv_screen_active();
    lv_group_t *old_group = lv_obj_get_user_data(old_scr);

    lv_indev_set_group(s_indev, group);
    if (focus) {
        lv_group_focus_obj(focus);
    }
    lv_screen_load(scr);
    lv_obj_delete(old_scr);
    if (old_group) {
        lv_group_delete(old_group);
    }
}

/* ---- menus --------------------------------------------------------------- */

static const char *ble_state_text(void)
{
    switch (s_ble_state) {
    case UI_BLE_UNPAIRED:
        return "new";
    case UI_BLE_PAIRING:
        return "pair";
    case UI_BLE_SECURING:
        return "..";
    case UI_BLE_CONNECTED:
        return "ok";
    case UI_BLE_ADVERTISING:
    default:
        return "--";
    }
}

static void update_status(void)
{
    if (!s_status) {
        return;
    }
    settings_t settings;
    settings_get(&settings);
    lv_label_set_text_fmt(s_status, "BLE %s  USB %s  %s", ble_state_text(),
                          s_usb_mounted ? "ok" : "--", settings.layout);
}

static void action_click_cb(lv_event_t *e)
{
    emit((ui_action_t)(intptr_t)lv_event_get_user_data(e), 0);
}

static void submenu_cb(lv_event_t *e)
{
    navigate((menu_fn_t)lv_event_get_user_data(e));
}

static void layout_select_cb(lv_event_t *e)
{
    emit(UI_ACTION_SET_LAYOUT, (int)(intptr_t)lv_event_get_user_data(e));
    navigate(show_main_menu);
}

static void speed_select_cb(lv_event_t *e)
{
    emit(UI_ACTION_SET_SPEED, (int)(intptr_t)lv_event_get_user_data(e));
    navigate(show_main_menu);
}

static void forget_confirm_cb(lv_event_t *e)
{
    emit(UI_ACTION_FORGET_PHONE, 0);
    navigate(show_main_menu);
}

static void show_layout_menu(void)
{
    settings_t settings;
    settings_get(&settings);
    lv_obj_t *list = menu_begin("Keyboard layout", show_main_menu);
    lv_obj_t *focus = NULL;
    for (size_t i = 0; i < keymap_layout_count(); i++) {
        const keymap_layout_t *layout = keymap_layout_get(i);
        lv_obj_t *item = menu_add(list, layout->name, layout_select_cb, (intptr_t)i);
        if (strcmp(layout->id, settings.layout) == 0) {
            focus = item;
        }
    }
    menu_end(list, focus);
}

static void show_speed_menu(void)
{
    settings_t settings;
    settings_get(&settings);
    lv_obj_t *list = menu_begin("Typing speed", show_main_menu);
    lv_obj_t *focus = NULL;
    for (size_t i = 0; i < settings_speed_preset_count; i++) {
        const settings_speed_preset_t *preset = &settings_speed_presets[i];
        lv_obj_t *item = menu_add(list, preset->name, speed_select_cb, (intptr_t)i);
        if (preset->key_press_ms == settings.key_press_ms) {
            focus = item;
        }
    }
    menu_end(list, focus);
}

static void show_forget_menu(void)
{
    lv_obj_t *list = menu_begin("Forget phone?", show_main_menu);
    menu_add(list, "Cancel", submenu_cb, (intptr_t)show_main_menu);
    menu_add(list, "Forget", forget_confirm_cb, 0);
    menu_end(list, NULL);
}

static void show_about(void)
{
    lv_obj_t *list = menu_begin("About", show_main_menu);
    lv_obj_t *info = lv_label_create(list);
    lv_label_set_text_fmt(info, "BLE Input Bridge\nIDF %s", esp_get_idf_version());
    menu_add(list, "Back", submenu_cb, (intptr_t)show_main_menu);
    menu_end(list, NULL);
}

static void show_main_menu(void)
{
    settings_t settings;
    settings_get(&settings);
    char text[32];

    lv_obj_t *list = menu_begin("", NULL);
    s_status = lv_obj_get_child(lv_obj_get_screen(list), 0);
    update_status();

    menu_add(list, "Type test", action_click_cb, UI_ACTION_TYPE_TEST);
    const keymap_layout_t *layout = keymap_layout_find(settings.layout);
    snprintf(text, sizeof(text), "Layout: %s", layout ? layout->name : "?");
    menu_add(list, text, submenu_cb, (intptr_t)show_layout_menu);
    const char *speed = "Custom";
    for (size_t i = 0; i < settings_speed_preset_count; i++) {
        if (settings_speed_presets[i].key_press_ms == settings.key_press_ms) {
            speed = settings_speed_presets[i].name;
        }
    }
    snprintf(text, sizeof(text), "Speed: %s", speed);
    menu_add(list, text, submenu_cb, (intptr_t)show_speed_menu);
    menu_add(list, "Pair new phone", action_click_cb, UI_ACTION_PAIR_NEW);
    menu_add(list, "Forget phone", submenu_cb, (intptr_t)show_forget_menu);
    menu_add(list, "About", submenu_cb, (intptr_t)show_about);
    menu_end(list, NULL);
}

/* ---- overlays ------------------------------------------------------------ */

static void overlay_click_cb(lv_event_t *e)
{
    emit((ui_action_t)(intptr_t)lv_event_get_user_data(e), 0);
}

static void overlay_key_cb(lv_event_t *e)
{
    if (lv_event_get_key(e) == LV_KEY_ESC) {
        emit((ui_action_t)(intptr_t)lv_event_get_user_data(e), 0);
    }
}

/* One "hold to <verb>" button; double click does the same. */
static lv_obj_t *overlay_begin(const char *title, const char *button, ui_action_t action, lv_obj_t **btn_out)
{
    lv_obj_t *list = menu_begin(title, NULL);
    lv_obj_t *btn = menu_add(list, button, overlay_click_cb, action);
    lv_obj_remove_event_cb(btn, item_key_cb);
    lv_obj_add_event_cb(btn, overlay_key_cb, LV_EVENT_KEY, (void *)(intptr_t)action);
    *btn_out = btn;
    return list;
}

/* Shows no counts or text: either would leak password length or content. */
static void show_typing(void)
{
    lv_obj_t *btn;
    lv_obj_t *list = overlay_begin("Typing...", "Hold to abort", UI_ACTION_ABORT_TYPING, &btn);
    menu_end(list, NULL);
}

static void show_pairing(void)
{
    settings_t settings;
    settings_get(&settings);
    lv_obj_t *btn;
    lv_obj_t *list = overlay_begin("Pair new phone", "Hold to cancel", UI_ACTION_CANCEL_PAIRING, &btn);
    lv_obj_t *info = lv_label_create(list);
    lv_label_set_text_fmt(info, "Connect to %s", settings.ble_name);
    lv_obj_move_to_index(info, 0);
    menu_end(list, NULL);
}

static void show_passkey(void)
{
    lv_obj_t *btn;
    lv_obj_t *list = overlay_begin("Enter on phone:", "Hold to cancel", UI_ACTION_CANCEL_PAIRING, &btn);
    lv_obj_t *code = lv_label_create(list);
    lv_obj_set_style_text_font(code, &lv_font_montserrat_24, 0);
    lv_label_set_text_fmt(code, "%03lu %03lu", (unsigned long)(s_passkey / 1000), (unsigned long)(s_passkey % 1000));
    lv_obj_set_width(code, lv_pct(100));
    lv_obj_set_style_text_align(code, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_move_to_index(code, 0);
    menu_end(list, NULL);
}

/* Must hold the LVGL lock. */
static void update_overlay(void)
{
    overlay_t want = s_passkey_shown ? OVERLAY_PASSKEY
                     : s_typing      ? OVERLAY_TYPING
                     : s_pairing     ? OVERLAY_PAIRING
                     : OVERLAY_NONE;
    if (want == s_overlay) {
        return;
    }
    s_overlay = want;
    screen_set(true);
    lv_display_trigger_activity(NULL);

    static const menu_fn_t screens[] = {
        [OVERLAY_NONE] = show_main_menu,
        [OVERLAY_PAIRING] = show_pairing,
        [OVERLAY_TYPING] = show_typing,
        [OVERLAY_PASSKEY] = show_passkey,
    };
    navigate(screens[want]);
}

/* ---- public -------------------------------------------------------------- */

void ui_set_usb_state(bool mounted)
{
    if (!lvgl_port_lock(0)) {
        return;
    }
    s_usb_mounted = mounted;
    update_status();
    lvgl_port_unlock();
}

void ui_set_ble_state(ui_ble_state_t state)
{
    if (!lvgl_port_lock(0)) {
        return;
    }
    s_ble_state = state;
    update_status();
    lvgl_port_unlock();
}

void ui_show_typing(bool typing)
{
    if (!lvgl_port_lock(0)) {
        return;
    }
    s_typing = typing;
    update_overlay();
    lvgl_port_unlock();
}

void ui_show_pairing(bool open)
{
    if (!lvgl_port_lock(0)) {
        return;
    }
    s_pairing = open;
    update_overlay();
    lvgl_port_unlock();
}

void ui_show_passkey(uint32_t passkey, bool show)
{
    if (!lvgl_port_lock(0)) {
        return;
    }
    s_passkey = passkey;
    s_passkey_shown = show;
    update_overlay();
    lvgl_port_unlock();
}

esp_err_t ui_start(button_handle_t btn, ui_action_cb_t action_cb)
{
    if (!lvgl_port_lock(0)) {
        return ESP_ERR_TIMEOUT;
    }
    s_action_cb = action_cb;

    lv_display_t *disp = lv_display_get_default();
    lv_theme_t *theme = lv_theme_default_init(disp, lv_palette_main(LV_PALETTE_BLUE),
                                              lv_palette_main(LV_PALETTE_CYAN), true, LV_FONT_DEFAULT);
    lv_display_set_theme(disp, theme);

    esp_err_t ret = ESP_OK;
    s_indev = ui_input_create(btn);
    if (s_indev) {
        show_main_menu();
        lv_timer_create(screen_timer_cb, SCREEN_TICK_MS, NULL);
        screen_set(false);
    } else {
        ret = ESP_ERR_NO_MEM;
    }

    lvgl_port_unlock();
    ESP_LOGI(TAG, "started");
    return ret;
}

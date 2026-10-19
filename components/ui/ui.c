#include <stdio.h>
#include "ui.h"
#include "ui_input.h"
#include "ui_priv.h"
#include "bsp/tdongle.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "settings.h"

static const char *TAG = "ui";

#define SCREEN_TICK_MS      250
#define HOME_RESET_MS       (2 * 60 * 1000)
#define ITEM_HEIGHT         20

/* Screens driven by device state rather than navigation, highest priority last. */
typedef enum {
    OVERLAY_NONE,
    OVERLAY_PAIRING,
    OVERLAY_TYPING,
    OVERLAY_FIDO,
    OVERLAY_PASSKEY,
    OVERLAY_UPDATE,
} overlay_t;

static const ui_app_t *const s_apps[] = {
    &ui_app_jiggler,
    &ui_app_settings,
};
#define APP_COUNT (sizeof(s_apps) / sizeof(s_apps[0]))

static void show_home(void);

static ui_action_cb_t s_action_cb;
static lv_indev_t *s_indev;
static lv_obj_t *s_status;
static ui_screen_fn_t s_back;
static volatile bool s_screen_on = true;
static uint32_t s_screen_off_tick;
static bool s_usb_mounted;
static ui_ble_state_t s_ble_state = UI_BLE_ADVERTISING;
static bool s_typing;
static bool s_fido_waiting;
static bool s_pairing;
static bool s_passkey_shown;
static uint32_t s_passkey;
static bool s_updating;
static uint8_t s_update_percent;
static lv_obj_t *s_update_label;
static overlay_t s_overlay = OVERLAY_NONE;
static const ui_app_t *s_app;
static size_t s_launcher_index;
static lv_obj_t *s_carousel_icon;
static lv_obj_t *s_carousel_name;

/* ---- screen power -------------------------------------------------------- */

static void screen_set(bool on)
{
    if (on == s_screen_on) {
        return;
    }
    s_screen_on = on;
    if (on) {
        if (lv_tick_elaps(s_screen_off_tick) >= HOME_RESET_MS && s_overlay == OVERLAY_NONE) {
            ui_navigate(show_home);
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

void ui_emit(ui_action_t action, int arg)
{
    if (s_action_cb) {
        s_action_cb(action, arg);
    }
}

bool ui_host_busy(void)
{
    return s_typing;
}

/* ---- navigation and menus ------------------------------------------------ */

static ui_screen_fn_t s_pending_nav;

static void navigate_async_cb(void *arg)
{
    ui_screen_fn_t fn = s_pending_nav;
    s_pending_nav = NULL;
    if (fn) {
        fn();
    }
}

void ui_navigate(ui_screen_fn_t fn)
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
        ui_navigate(s_back);
    } else {
        screen_set(false);
    }
}

static lv_obj_t *screen_begin(ui_screen_fn_t back)
{
    s_back = back;
    s_status = NULL;
    s_update_label = NULL;
    s_carousel_icon = NULL;
    s_carousel_name = NULL;

    lv_group_t *group = lv_group_create();
    lv_group_set_wrap(group, true);
    lv_group_set_default(group);

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(scr, 2, 0);
    lv_obj_set_style_pad_row(scr, 2, 0);
    lv_obj_set_user_data(scr, group);
    return scr;
}

static void screen_end(lv_obj_t *scr, lv_obj_t *focus)
{
    lv_group_t *group = lv_obj_get_user_data(scr);
    lv_obj_t *old_scr = lv_screen_active();
    lv_group_t *old_group = lv_obj_get_user_data(old_scr);

    lv_indev_set_group(s_indev, group);
    /* A hold that opened this screen must not keep pressing whatever is focused on it. */
    if (lv_indev_get_state(s_indev) == LV_INDEV_STATE_PRESSED) {
        lv_indev_wait_release(s_indev);
    }
    if (focus) {
        lv_group_focus_obj(focus);
    }
    lv_screen_load(scr);
    lv_obj_delete(old_scr);
    if (old_group) {
        lv_group_delete(old_group);
    }
}

lv_obj_t *ui_menu_begin(const char *title, ui_screen_fn_t back)
{
    lv_obj_t *scr = screen_begin(back);

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

lv_obj_t *ui_menu_add(lv_obj_t *list, const char *text, lv_event_cb_t cb, intptr_t arg)
{
    lv_obj_t *btn = lv_button_create(list);
    lv_obj_set_size(btn, lv_pct(100), ITEM_HEIGHT);
    lv_obj_add_event_cb(btn, item_key_cb, LV_EVENT_KEY, NULL);
    if (cb) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_PRESSED, (void *)arg);
    }

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return btn;
}

typedef struct {
    uint32_t duration_ms;
    uint32_t start;
    bool fired;
    lv_obj_t *bar;
    ui_screen_fn_t cb;
} hold_state_t;

static void hold_event_cb(lv_event_t *e)
{
    hold_state_t *hold = lv_event_get_user_data(e);
    switch (lv_event_get_code(e)) {
    case LV_EVENT_PRESSED:
        hold->start = lv_tick_get();
        hold->fired = false;
        break;
    case LV_EVENT_PRESSING: {
        if (hold->fired) {
            break;
        }
        uint32_t elapsed = lv_tick_elaps(hold->start);
        if (elapsed >= hold->duration_ms) {
            hold->fired = true;
            lv_bar_set_value(hold->bar, 100, LV_ANIM_OFF);
            hold->cb();
        } else {
            lv_bar_set_value(hold->bar, (int32_t)(elapsed * 100 / hold->duration_ms), LV_ANIM_OFF);
        }
        break;
    }
    case LV_EVENT_RELEASED:
        lv_bar_set_value(hold->bar, 0, LV_ANIM_OFF);
        break;
    case LV_EVENT_DELETE:
        lv_free(hold);
        break;
    default:
        break;
    }
}

lv_obj_t *ui_hold_add(lv_obj_t *list, const char *text, uint32_t duration_ms, ui_screen_fn_t cb)
{
    hold_state_t *hold = lv_malloc_zeroed(sizeof(*hold));
    LV_ASSERT_MALLOC(hold);
    hold->duration_ms = duration_ms;
    hold->cb = cb;

    lv_obj_t *btn = ui_menu_add(list, NULL, NULL, 0);
    lv_obj_t *label = lv_obj_get_child(btn, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);

    hold->bar = lv_bar_create(btn);
    lv_obj_set_size(hold->bar, lv_pct(100), lv_pct(100));
    lv_obj_set_style_radius(hold->bar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(hold->bar, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(hold->bar, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_color(hold->bar, lv_palette_main(LV_PALETTE_RED), LV_PART_INDICATOR);
    lv_bar_set_range(hold->bar, 0, 100);
    lv_obj_remove_flag(hold->bar, LV_OBJ_FLAG_CLICKABLE);

    lv_label_set_text(label, text);
    lv_obj_move_foreground(label);
    lv_obj_center(label);
    lv_obj_add_event_cb(btn, hold_event_cb, LV_EVENT_ALL, hold);
    return btn;
}

void ui_menu_end(lv_obj_t *list, lv_obj_t *focus)
{
    screen_end(lv_obj_get_screen(list), focus);
}

void ui_submenu_cb(lv_event_t *e)
{
    ui_navigate((ui_screen_fn_t)lv_event_get_user_data(e));
}

/* ---- launcher ------------------------------------------------------------ */

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

static void show_carousel_item(void)
{
    if (s_carousel_icon) {
        lv_label_set_text(s_carousel_icon, s_apps[s_launcher_index]->icon);
        lv_label_set_text_fmt(s_carousel_name, "%s  %u/%u", s_apps[s_launcher_index]->name,
                              (unsigned)(s_launcher_index + 1), (unsigned)APP_COUNT);
    }
}

static void open_selected_app(void)
{
    s_app = s_apps[s_launcher_index];
    ESP_LOGI(TAG, "starting %s", s_app->name);
    if (s_app->start) {
        s_app->start();
    }
    s_app->show();
}

/*
 * The carousel keeps one invisible focusable object per app: the group's next/prev handling then
 * moves between apps exactly like menu items, and focus changes update the visible icon.
 */
static void carousel_event_cb(lv_event_t *e)
{
    size_t index = (size_t)(uintptr_t)lv_event_get_user_data(e);
    /* Deleting the launcher moves group focus between slots: those events aren't the user's. */
    if (lv_obj_get_screen(lv_event_get_target_obj(e)) != lv_screen_active()) {
        return;
    }
    switch (lv_event_get_code(e)) {
    case LV_EVENT_FOCUSED:
        s_launcher_index = index;
        show_carousel_item();
        break;
    case LV_EVENT_PRESSED:
        s_launcher_index = index;
        ui_navigate(open_selected_app);
        break;
    default:
        break;
    }
}

static void show_launcher(void)
{
    lv_obj_t *scr = screen_begin(NULL);
    s_status = lv_label_create(scr);
    update_status();

    lv_obj_t *focus = NULL;
    for (size_t i = 0; i < APP_COUNT; i++) {
        lv_obj_t *slot = lv_obj_create(scr);
        lv_obj_remove_style_all(slot);
        lv_obj_add_flag(slot, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_remove_flag(slot, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
        lv_obj_set_size(slot, 1, 1);
        lv_group_add_obj(lv_group_get_default(), slot);
        lv_obj_add_event_cb(slot, carousel_event_cb, LV_EVENT_ALL, (void *)(uintptr_t)i);
        lv_obj_add_event_cb(slot, item_key_cb, LV_EVENT_KEY, NULL);
        if (i == s_launcher_index) {
            focus = slot;
        }
    }

    s_carousel_icon = lv_label_create(scr);
    lv_obj_set_style_text_font(s_carousel_icon, &lv_font_montserrat_24, 0);
    lv_obj_set_width(s_carousel_icon, lv_pct(100));
    lv_obj_set_style_text_align(s_carousel_icon, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_flex_grow(s_carousel_icon, 1);

    s_carousel_name = lv_label_create(scr);
    lv_obj_set_width(s_carousel_name, lv_pct(100));
    lv_obj_set_style_text_align(s_carousel_name, LV_TEXT_ALIGN_CENTER, 0);

    show_carousel_item();
    screen_end(scr, focus);
}

/* The running app's root screen, or the launcher when no app runs. */
static void show_home(void)
{
    if (s_app) {
        s_app->show();
    } else {
        show_launcher();
    }
}

void ui_app_exit(void)
{
    if (!s_app) {
        return;
    }
    ESP_LOGI(TAG, "leaving %s", s_app->name);
    const ui_app_t *app = s_app;
    s_app = NULL;
    if (app->stop) {
        app->stop();
    }
    ui_navigate(show_launcher);
}

/* ---- overlays ------------------------------------------------------------ */

static void overlay_click_cb(lv_event_t *e)
{
    ui_emit((ui_action_t)(intptr_t)lv_event_get_user_data(e), 0);
}

static void overlay_key_cb(lv_event_t *e)
{
    if (lv_event_get_key(e) == LV_KEY_ESC) {
        ui_emit((ui_action_t)(intptr_t)lv_event_get_user_data(e), 0);
    }
}

/* One "hold to <verb>" button; double click does the same. */
static lv_obj_t *overlay_begin(const char *title, const char *button, ui_action_t action)
{
    lv_obj_t *list = ui_menu_begin(title, NULL);
    lv_obj_t *btn = ui_menu_add(list, button, overlay_click_cb, action);
    lv_obj_remove_event_cb(btn, item_key_cb);
    lv_obj_add_event_cb(btn, overlay_key_cb, LV_EVENT_KEY, (void *)(intptr_t)action);
    return list;
}

/* Shows no counts or text: either would leak password length or content. */
static void show_fido(void)
{
    lv_obj_t *list = overlay_begin("Security key", "Hold to confirm", UI_ACTION_FIDO_CONFIRM);
    lv_obj_t *info = lv_label_create(list);
    lv_label_set_text(info, "Sign-in request");
    lv_obj_move_to_index(info, 0);
    ui_menu_end(list, NULL);
}

static void show_typing(void)
{
    ui_menu_end(overlay_begin("Typing...", "Hold to abort", UI_ACTION_ABORT_TYPING), NULL);
}

static void show_pairing(void)
{
    settings_t settings;
    settings_get(&settings);
    lv_obj_t *list = overlay_begin("Pair new phone", "Hold to cancel", UI_ACTION_CANCEL_PAIRING);
    lv_obj_t *info = lv_label_create(list);
    lv_label_set_text_fmt(info, "Connect to %s", settings.ble_name);
    lv_obj_move_to_index(info, 0);
    ui_menu_end(list, NULL);
}

static void show_passkey(void)
{
    lv_obj_t *list = overlay_begin("Enter on phone:", "Hold to cancel", UI_ACTION_CANCEL_PAIRING);
    lv_obj_t *code = lv_label_create(list);
    lv_obj_set_style_text_font(code, &lv_font_montserrat_24, 0);
    lv_label_set_text_fmt(code, "%03lu %03lu", (unsigned long)(s_passkey / 1000), (unsigned long)(s_passkey % 1000));
    lv_obj_set_width(code, lv_pct(100));
    lv_obj_set_style_text_align(code, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_move_to_index(code, 0);
    ui_menu_end(list, NULL);
}

static void set_update_label(void)
{
    if (s_update_label) {
        lv_label_set_text_fmt(s_update_label, "%u%%", s_update_percent);
    }
}

static void show_update(void)
{
    lv_obj_t *list = ui_menu_begin("Updating firmware", NULL);
    s_update_label = lv_label_create(list);
    lv_obj_set_style_text_font(s_update_label, &lv_font_montserrat_24, 0);
    lv_obj_set_width(s_update_label, lv_pct(100));
    lv_obj_set_style_text_align(s_update_label, LV_TEXT_ALIGN_CENTER, 0);
    set_update_label();
    ui_menu_end(list, NULL);
}

static void update_overlay(void)
{
    overlay_t want = s_updating      ? OVERLAY_UPDATE
                     : s_passkey_shown ? OVERLAY_PASSKEY
                     : s_fido_waiting ? OVERLAY_FIDO
                     : s_typing      ? OVERLAY_TYPING
                     : s_pairing     ? OVERLAY_PAIRING
                     : OVERLAY_NONE;
    if (want == s_overlay) {
        return;
    }
    s_overlay = want;
    /* Typing never wakes the screen: it only shows if the screen was already on. */
    if (want != OVERLAY_NONE && want != OVERLAY_TYPING) {
        screen_set(true);
    }
    if (s_screen_on) {
        lv_display_trigger_activity(NULL);
    }

    static const ui_screen_fn_t screens[] = {
        [OVERLAY_NONE] = show_home,
        [OVERLAY_PAIRING] = show_pairing,
        [OVERLAY_TYPING] = show_typing,
        [OVERLAY_FIDO] = show_fido,
        [OVERLAY_PASSKEY] = show_passkey,
        [OVERLAY_UPDATE] = show_update,
    };
    ui_navigate(screens[want]);
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

static const char *s_fido_status = "off";

void ui_set_fido_status(const char *status)
{
    s_fido_status = status;
}

const char *ui_fido_status(void)
{
    return s_fido_status;
}

void ui_show_fido_presence(bool waiting)
{
    if (!lvgl_port_lock(0)) {
        return;
    }
    s_fido_waiting = waiting;
    update_overlay();
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

void ui_show_update(bool active, uint8_t percent)
{
    if (!lvgl_port_lock(0)) {
        return;
    }
    s_updating = active;
    s_update_percent = percent;
    set_update_label();
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
        show_launcher();
        lv_timer_create(screen_timer_cb, SCREEN_TICK_MS, NULL);
        screen_set(false);
    } else {
        ret = ESP_ERR_NO_MEM;
    }

    lvgl_port_unlock();
    ESP_LOGI(TAG, "started");
    return ret;
}

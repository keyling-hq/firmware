#include <math.h>
#include <stdio.h>
#include <string.h>
#include "ui.h"
#include "ui_input.h"
#include "ui_theme.h"
#include "ui_nav.h"
#include "bsp/tdongle.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "settings.h"
#include "status_led.h"
#include "src/libs/qrcode/qrcodegen.h"
#include "src/misc/cache/instance/lv_image_cache.h"

static const char *TAG = "ui";

#define SCREEN_TICK_MS      250
#define HOME_RESET_MS       (2 * 60 * 1000)
#define SLIDE_MS            260
#define TICKER_PX_PER_S     14
#define TICKER_PAUSE_MS     1500
#define TICKER_GAP          24
#define SCROLL_PX_PER_S     60
#define SCROLL_PAUSE_MS     500
#define SCROLL_GAP          24
#define CAREFUL_MIN_PX_PER_S    20
#define CAREFUL_MAX_PX_PER_S    50
#define CAREFUL_TRAVEL_MS       3000
#define CAREFUL_PAUSE_MS        1500
#define BLE_CONNECTED_COLOR 0x4D9BFF
#define STATUS_TICK_MS      33
/* Waits for the lock: dropping these would leave a request without its prompt. */
#define PROMPT_LOCK_MS      200

/* Screens driven by device state rather than navigation, highest priority last. */
typedef enum {
    OVERLAY_NONE,
    OVERLAY_PAIRING,
    OVERLAY_TYPING,
    OVERLAY_FIDO_RESULT,
    OVERLAY_FIDO,
    OVERLAY_PASSKEY,
    OVERLAY_UPDATE,
} overlay_t;

/* Launcher order. Apps other than Settings can be left out of a build (Kconfig, "Keyling UI"). */
static const ui_app_t *const s_apps[] = {
#if CONFIG_UI_APP_JIGGLER
    &ui_app_jiggler,
#endif
#if CONFIG_UI_APP_PROFILES
    &ui_app_profiles,
#endif
#if CONFIG_UI_APP_DRIVE
    &ui_app_drive,
#endif
    &ui_app_settings,
#if CONFIG_UI_APP_EXAMPLE
    &ui_app_example,
#endif
};
#define APP_COUNT (sizeof(s_apps) / sizeof(s_apps[0]))

static const ui_theme_t *const s_themes[] = {
    &ui_theme_candy,
    &ui_theme_device,
#if CONFIG_UI_THEME_EXAMPLE
    &ui_theme_example,
#endif
};
#define THEME_COUNT (sizeof(s_themes) / sizeof(s_themes[0]))

static void show_home(void);
static void update_overlay(void);

static const ui_theme_t *s_theme = &ui_theme_candy;
static ui_action_cb_t s_action_cb;
static lv_indev_t *s_indev;
static ui_screen_fn_t s_back;
static volatile bool s_screen_on = true;
static uint32_t s_screen_off_tick;
static bool s_usb_mounted;
static ui_ble_state_t s_ble_state = UI_BLE_ADVERTISING;
static bool s_typing;
static bool s_fido_waiting;
static ui_fido_request_t s_fido_kind;
static char s_fido_rp_id[64];
static bool s_fido_result_shown;
static bool s_fido_approved;
static bool s_fido_locked;
static lv_timer_t *s_fido_result_timer;
static ui_profile_t s_profile_active;
static ui_profile_t s_profile_default;
static ui_phantom_state_t s_phantom_state = UI_PHANTOM_NEW;
static bool s_pairing;
static uint32_t s_pairing_passkey;
static char s_pairing_qr[UI_PAIRING_QR_MAX];
static bool s_passkey_shown;
static uint32_t s_passkey;
static bool s_updating;
static uint8_t s_update_percent;
static lv_obj_t *s_update_label;
static overlay_t s_overlay = OVERLAY_NONE;
static const ui_app_t *s_app;
static size_t s_launcher_index;
static ui_launcher_t s_launcher;

/* ---- themes ------------------------------------------------------------------------------ */

const ui_theme_t *ui_theme(void)
{
    return s_theme;
}

const ui_theme_t *ui_theme_at(size_t index)
{
    return index < THEME_COUNT ? s_themes[index] : NULL;
}

const char *ui_theme_id(size_t index)
{
    return index < THEME_COUNT ? s_themes[index]->id : NULL;
}

const char *ui_theme_name(size_t index)
{
    return index < THEME_COUNT ? s_themes[index]->name : NULL;
}

size_t ui_theme_index(void)
{
    for (size_t i = 0; i < THEME_COUNT; i++) {
        if (s_themes[i] == s_theme) {
            return i;
        }
    }
    return 0;
}

static void theme_load(void)
{
    settings_t settings;
    settings_get(&settings);
    s_theme = s_themes[0];
    for (size_t i = 0; i < THEME_COUNT; i++) {
        if (strcmp(s_themes[i]->id, settings.theme) == 0) {
            s_theme = s_themes[i];
        }
    }
}

void ui_theme_select(size_t index)
{
    if (index < THEME_COUNT) {
        ui_emit(UI_ACTION_SET_THEME, (int)index);
        s_theme = s_themes[index];
    }
}

/* ---- helpers for themes ------------------------------------------------------------------ */

float ui_wrap(float x, int n)
{
    x = fmodf(x, (float)n);
    if (x < -n / 2.0f) {
        x += n;
    } else if (x >= n / 2.0f) {
        x -= n;
    }
    return x;
}

bool ui_menu_loops(const ui_menu_t *m)
{
    return (m->kind == UI_MENU_LIST || m->kind == UI_MENU_DIALOG) && m->count >= 2;
}

float ui_menu_offset(const ui_menu_t *m, int i)
{
    return ui_menu_loops(m) ? ui_wrap(i - m->pos, m->count) : i - m->pos;
}

lv_color_t ui_mix(uint32_t a, uint32_t b, float t)
{
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    return lv_color_mix(lv_color_hex(b), lv_color_hex(a), (uint8_t)(t * 255));
}

uint32_t ui_tone_color(ui_tone_t tone)
{
    switch (tone) {
    case UI_TONE_GOOD:
        return s_theme->pal.good;
    case UI_TONE_BAD:
        return s_theme->pal.bad;
    case UI_TONE_WARN:
        return s_theme->pal.warn;
    default:
        return s_theme->pal.text;
    }
}

const char *ui_tone_symbol(ui_tone_t tone)
{
    switch (tone) {
    case UI_TONE_GOOD:
        return LV_SYMBOL_OK;
    case UI_TONE_BAD:
        return LV_SYMBOL_CLOSE;
    case UI_TONE_WARN:
        return LV_SYMBOL_WARNING;
    default:
        return "";
    }
}

/*
 * The Bluetooth symbol shows what the status LED shows, in step with it: the LED's color at full
 * strength, faded towards dim as the LED dims. Once connected and the LED's flash is over, it stays
 * BLE_CONNECTED_COLOR.
 */
uint32_t ui_ble_color(const ui_status_t *s)
{
    status_led_pattern_t pattern;
    uint32_t ms;
    bool done;
    status_led_base(&pattern, &ms);
    status_led_color_t c = status_led_render(pattern, ms, &done);
    uint8_t peak = c.r > c.g ? (c.r > c.b ? c.r : c.b) : (c.g > c.b ? c.g : c.b);
    if (peak == 0) {
        return s->ble == UI_BLE_CONNECTED ? BLE_CONNECTED_COLOR : s_theme->pal.dim;
    }
    uint32_t full = (uint32_t)(c.r * 255 / peak) << 16 | (uint32_t)(c.g * 255 / peak) << 8 | (c.b * 255 / peak);
    return lv_color_to_u32(ui_mix(s_theme->pal.dim, full, (float)peak / STATUS_LED_PEAK)) & 0xFFFFFF;
}

uint32_t ui_usb_color(const ui_status_t *s)
{
    return s->usb ? s_theme->pal.good : s_theme->pal.dim;
}

lv_obj_t *ui_box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t bg, int radius)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

#define MIN_CONTRAST    2.0f

lv_obj_t *ui_site_mark(lv_obj_t *parent, const ui_site_t *site, int size, uint32_t color, uint32_t ink,
                       const lv_font_t *font)
{
    if (site->icon) {
        lv_obj_t *img = lv_image_create(parent);
        lv_image_set_src(img, site->icon);
        lv_obj_set_style_image_recolor(img, lv_color_hex(color), 0);
        lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
        return img;
    }
    if (!font) {
        size_t len = strlen(site->badge);
        bool big = size >= 32;
        font = len > 2   ? (big ? &lv_font_montserrat_12 : &lv_font_montserrat_10)
               : len > 1 ? (big ? &lv_font_montserrat_14 : &lv_font_montserrat_12)
                         : (big ? &lv_font_montserrat_20 : &lv_font_montserrat_16);
    }
    lv_obj_t *badge = ui_box(parent, 0, 0, LV_SIZE_CONTENT, size, color, LV_RADIUS_CIRCLE);
    lv_obj_set_style_min_width(badge, size, 0);
    lv_obj_set_style_pad_hor(badge, 5, 0);
    lv_obj_center(ui_label(badge, font, ink, site->badge));
    return badge;
}

static float luminance(uint32_t rgb)
{
    float c[3] = { (rgb >> 16 & 0xFF) / 255.0f, (rgb >> 8 & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f };
    for (int i = 0; i < 3; i++) {
        c[i] = c[i] <= 0.04045f ? c[i] / 12.92f : powf((c[i] + 0.055f) / 1.055f, 2.4f);
    }
    return 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2];
}

/* WCAG contrast ratio. */
static float contrast(uint32_t a, uint32_t b)
{
    float la = luminance(a), lb = luminance(b);
    return la > lb ? (la + 0.05f) / (lb + 0.05f) : (lb + 0.05f) / (la + 0.05f);
}

uint32_t ui_site_color(const ui_site_t *site, uint32_t bg, uint32_t fallback)
{
    return contrast(site->color, bg) >= MIN_CONTRAST ? site->color : fallback;
}

uint32_t ui_ink_on(uint32_t bg, uint32_t light, uint32_t dark)
{
    return contrast(bg, light) >= contrast(bg, dark) ? light : dark;
}

lv_obj_t *ui_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_label_set_text(l, text);
    return l;
}

/*
 * Scrolling labels: a clipping box holding the text and, when the text doesn't fit, a
 * second copy one period after it, so moving both by a period loops seamlessly. Text that fits
 * stays still. A label scrolls only while active (focused items, or always outside items), and
 * always restarts from the beginning after a pause.
 */
static const char s_scroll_tag = 0;
static int s_scroll_px_per_s = SCROLL_PX_PER_S;

int ui_scroll_speed(void)
{
    return s_scroll_px_per_s;
}

void ui_scroll_speed_set(int px_per_s)
{
    s_scroll_px_per_s = px_per_s > 0 ? px_per_s : SCROLL_PX_PER_S;
}

/* Careful scrolling: the text, and the thumb (its user data) in step across its track. */
static void careful_anim_cb(void *var, int32_t x)
{
    lv_obj_t *text = var;
    lv_obj_set_x(text, x);
    lv_obj_t *thumb = lv_obj_get_user_data(text);
    int32_t overflow = lv_obj_get_width(text) - lv_obj_get_width(lv_obj_get_parent(text));
    if (thumb && overflow > 0) {
        int32_t room = lv_obj_get_width(lv_obj_get_parent(thumb)) - lv_obj_get_width(thumb);
        lv_obj_set_x(thumb, -x * room / overflow);
    }
}

static void careful_set(lv_obj_t *box, lv_obj_t *text, int32_t text_w, int32_t width, bool active)
{
    lv_obj_t *thumb = lv_obj_get_user_data(text);
    if (thumb) {
        lv_obj_t *track = lv_obj_get_parent(thumb);
        lv_obj_update_layout(track);
        if (text_w <= width) {
            lv_obj_add_flag(track, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(track, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_width(thumb, lv_obj_get_width(track) * width / text_w);
        }
    }
    lv_obj_set_width(text, text_w);
    lv_obj_update_layout(text);
    careful_anim_cb(text, 0);
    if (!active || text_w <= width) {
        return;
    }
    int32_t overflow = text_w - width;
    int32_t speed = overflow * 1000 / CAREFUL_TRAVEL_MS;
    speed = speed < CAREFUL_MIN_PX_PER_S ? CAREFUL_MIN_PX_PER_S : speed > CAREFUL_MAX_PX_PER_S ? CAREFUL_MAX_PX_PER_S : speed;
    uint32_t travel_ms = (uint32_t)(overflow * 1000 / speed);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, text);
    lv_anim_set_exec_cb(&a, careful_anim_cb);
    lv_anim_set_values(&a, 0, -overflow);
    lv_anim_set_duration(&a, travel_ms);
    lv_anim_set_reverse_duration(&a, travel_ms);
    lv_anim_set_delay(&a, CAREFUL_PAUSE_MS);
    lv_anim_set_reverse_delay(&a, CAREFUL_PAUSE_MS);
    lv_anim_set_repeat_delay(&a, CAREFUL_PAUSE_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
}

/* Moves the text and, one period behind it, its copy. */
static void scroll_anim_cb(lv_anim_t *a, int32_t v)
{
    lv_obj_t *box = a->var;
    lv_obj_set_x(lv_obj_get_child(box, 0), v);
    lv_obj_set_x(lv_obj_get_child(box, 1), v + (int32_t)(intptr_t)lv_anim_get_user_data(a));
}

static void scroll_set(lv_obj_t *box, bool active, bool force)
{
    if (!force && active == lv_obj_has_flag(box, LV_OBJ_FLAG_USER_1)) {
        return;
    }
    if (active) {
        lv_obj_add_flag(box, LV_OBJ_FLAG_USER_1);
    } else {
        lv_obj_remove_flag(box, LV_OBJ_FLAG_USER_1);
    }
    lv_anim_delete(box, NULL);
    lv_obj_t *text = lv_obj_get_child(box, 0);
    lv_anim_delete(text, NULL);
    lv_point_t size;
    lv_text_get_size(&size, lv_label_get_text(text), lv_obj_get_style_text_font(box, 0), 0, 0, LV_COORD_MAX,
                     LV_TEXT_FLAG_NONE);
    int32_t width = lv_obj_get_style_width(box, 0);
    if (lv_obj_has_flag(box, LV_OBJ_FLAG_USER_2)) {
        int32_t max = lv_obj_get_style_max_width(box, 0);
        width = size.x < max ? size.x : max;
        lv_obj_set_width(box, width);
    }
    if (lv_obj_has_flag(box, LV_OBJ_FLAG_USER_3)) {
        careful_set(box, text, size.x, width, active);
        return;
    }
    if (size.x <= width) {
        lv_text_align_t align = lv_obj_get_style_text_align(box, 0);
        lv_obj_set_x(text, align == LV_TEXT_ALIGN_RIGHT ? width - size.x
                           : align == LV_TEXT_ALIGN_CENTER ? (width - size.x) / 2 : 0);
        return;
    }
    /* Only text that overflows gets the copy that makes the loop seamless. */
    int32_t period = size.x + SCROLL_GAP;
    lv_obj_t *copy = lv_obj_get_child(box, 1);
    if (!copy) {
        copy = lv_label_create(box);
        lv_label_set_text(copy, lv_label_get_text(text));
    }
    lv_obj_set_x(text, 0);
    lv_obj_set_x(copy, period);
    if (!active) {
        return;
    }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, box);
    lv_anim_set_custom_exec_cb(&a, scroll_anim_cb);
    lv_anim_set_user_data(&a, (void *)(intptr_t)period);
    lv_anim_set_values(&a, 0, -period);
    lv_anim_set_duration(&a, (uint32_t)period * 1000 / s_scroll_px_per_s);
    lv_anim_set_delay(&a, SCROLL_PAUSE_MS);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
}

void ui_scroll_activate(lv_obj_t *tree, bool active)
{
    if (lv_obj_get_user_data(tree) == &s_scroll_tag) {
        scroll_set(tree, active, false);
        return;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(tree); i++) {
        ui_scroll_activate(lv_obj_get_child(tree, i), active);
    }
}

lv_obj_t *ui_label_scroll(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text, int width)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(box, width, lv_font_get_line_height(font));
    lv_obj_set_style_text_font(box, font, 0);
    lv_obj_set_style_text_color(box, lv_color_hex(color), 0);
    lv_obj_set_user_data(box, (void *)&s_scroll_tag);
    lv_label_set_text(lv_label_create(box), text);
    scroll_set(box, true, true);
    return box;
}

void ui_label_scroll_align(lv_obj_t *box, lv_text_align_t align)
{
    lv_obj_set_style_text_align(box, align, 0);
    scroll_set(box, lv_obj_has_flag(box, LV_OBJ_FLAG_USER_1), true);
}

void ui_label_scroll_careful(lv_obj_t *box, lv_obj_t *thumb)
{
    lv_obj_add_flag(box, LV_OBJ_FLAG_USER_3);
    lv_obj_set_user_data(lv_obj_get_child(box, 0), thumb);
    lv_obj_t *copy = lv_obj_get_child(box, 1);
    if (copy) {
        lv_obj_delete(copy);
    }
    scroll_set(box, lv_obj_has_flag(box, LV_OBJ_FLAG_USER_1), true);
}

void ui_label_scroll_fit(lv_obj_t *box)
{
    lv_obj_set_style_max_width(box, lv_obj_get_style_width(box, 0), 0);
    lv_obj_add_flag(box, LV_OBJ_FLAG_USER_2);
    scroll_set(box, lv_obj_has_flag(box, LV_OBJ_FLAG_USER_1), true);
}

void ui_label_set_text(lv_obj_t *label, const char *text)
{
    if (lv_obj_get_user_data(label) != &s_scroll_tag) {
        lv_label_set_text(label, text);
        return;
    }
    lv_label_set_text(lv_obj_get_child(label, 0), text);
    lv_obj_t *copy = lv_obj_get_child(label, 1);
    if (copy) {
        lv_label_set_text(copy, text);
    }
    scroll_set(label, lv_obj_has_flag(label, LV_OBJ_FLAG_USER_1), true);
}

static void more_hint_anim_cb(void *var, int32_t v)
{
    lv_obj_set_style_translate_y(var, v, 0);
}

lv_obj_t *ui_more_hint(lv_obj_t *parent, uint32_t color)
{
    lv_obj_t *l = ui_label(parent, &lv_font_montserrat_10, color, LV_SYMBOL_DOWN);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, l);
    lv_anim_set_exec_cb(&a, more_hint_anim_cb);
    lv_anim_set_values(&a, -1, 2);
    lv_anim_set_duration(&a, 500);
    lv_anim_set_reverse_duration(&a, 500);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
    return l;
}

static void ticker_anim_cb(void *var, int32_t v)
{
    lv_obj_set_y(var, -v);
}

lv_obj_t *ui_ticker(lv_obj_t *parent, int x, int y, int w, int h, const lv_font_t *font, uint32_t color,
                    const char *text)
{
    lv_obj_t *area = ui_box(parent, x, y, w, h, 0, 0);
    lv_obj_set_style_bg_opa(area, LV_OPA_TRANSP, 0);
    lv_obj_t *strip = lv_obj_create(area);
    lv_obj_remove_style_all(strip);
    lv_obj_remove_flag(strip, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(strip, w, LV_SIZE_CONTENT);
    lv_obj_t *first = ui_label(strip, font, color, text);
    lv_obj_set_width(first, w);
    lv_obj_update_layout(first);
    int32_t text_h = lv_obj_get_height(first);
    if (text_h <= h) {
        return area;
    }
    /* A second copy below the first: scrolling one period ends where it started. */
    int32_t period = text_h + TICKER_GAP;
    lv_obj_t *second = ui_label(strip, font, color, text);
    lv_obj_set_width(second, w);
    lv_obj_set_y(second, period);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, strip);
    lv_anim_set_exec_cb(&a, ticker_anim_cb);
    lv_anim_set_values(&a, 0, period);
    lv_anim_set_duration(&a, (uint32_t)period * 1000 / TICKER_PX_PER_S);
    lv_anim_set_delay(&a, TICKER_PAUSE_MS);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
    return area;
}

/* ---- screen power ------------------------------------------------------------------------ */

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

bool ui_usb_mounted(void)
{
    return s_usb_mounted;
}

ui_ble_state_t ui_ble_state(void)
{
    return s_ble_state;
}

/* ---- navigation -------------------------------------------------------------------------- */

static ui_screen_fn_t s_pending_nav;

static void navigate_async_cb(void *arg)
{
    ui_screen_fn_t fn = s_pending_nav;
    s_pending_nav = NULL;
    if (fn) {
        fn();
    }
}

static void navigate(ui_screen_fn_t fn)
{
    if (!s_pending_nav) {
        lv_async_call(navigate_async_cb, NULL);
    }
    s_pending_nav = fn;
}

/* An overlay stays until its state ends: menus navigating in the meantime must not replace it. */
void ui_navigate(ui_screen_fn_t fn)
{
    if (s_overlay == OVERLAY_NONE) {
        navigate(fn);
    }
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

static ui_menu_t *s_menu;

/*
 * Empties the screen being left before the next one is built, so only one screen's objects exist
 * at a time: the LVGL heap needn't hold two (a menu rebuilt in place would double the largest).
 * Its group goes first, so deleting items moves no focus and sends no focus events.
 */
static void screen_leave(void)
{
    lv_obj_t *old = lv_screen_active();
    if (!old) {
        return;
    }
    if (s_menu) {
        lv_anim_delete(s_menu, NULL);
        s_menu = NULL;
    }
    lv_group_t *old_group = lv_obj_get_user_data(old);
    if (old_group) {
        if (s_indev) {
            lv_indev_set_group(s_indev, NULL);
        }
        lv_group_delete(old_group);
        lv_obj_set_user_data(old, NULL);
    }
    lv_obj_clean(old);
}

static lv_obj_t *screen_begin(ui_screen_fn_t back)
{
    s_back = back;
    s_update_label = NULL;
    lv_anim_delete(&s_launcher, NULL);
    s_launcher.scr = NULL;
    screen_leave();

    lv_group_t *group = lv_group_create();
    lv_group_set_wrap(group, true);
    lv_group_set_default(group);

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_size(scr, UI_W, UI_H);
    lv_obj_set_style_bg_color(scr, lv_color_hex(s_theme->pal.bg), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
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

/* An invisible object in the screen's group: receives the button where nothing is selectable. */
static lv_obj_t *focus_target(lv_obj_t *scr)
{
    lv_obj_t *o = lv_obj_create(scr);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, 0, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_group_add_obj(lv_group_get_default(), o);
    return o;
}

/* ---- menus ------------------------------------------------------------------------------- */

typedef struct {
    ui_menu_t *menu;
    ui_item_t *item;
    uint32_t duration_ms;
    uint32_t start;
    bool fired;
    ui_screen_fn_t cb;
} hold_state_t;

static float s_slide_from;

static void menu_slide_cb(void *var, int32_t v)
{
    ui_menu_t *m = var;
    m->pos = s_slide_from + (m->focus_target - s_slide_from) * v / 1024.0f;
    s_theme->menu_place(m);
}

static void menu_slide_to(ui_menu_t *m, int index)
{
    int delta = index - m->focus;
    if (ui_menu_loops(m)) {
        delta = (int)lroundf(ui_wrap((float)delta, m->count));
        /* Halfway round either way (two items): a click moves forward. */
        if (2 * delta == -m->count) {
            delta = -delta;
        }
    }
    ui_scroll_activate(m->items[m->focus].obj, false);
    ui_scroll_activate(m->items[index].obj, true);
    m->focus = index;
    m->focus_target += delta;
    s_slide_from = m->pos;
    lv_anim_delete(m, NULL);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, m);
    lv_anim_set_exec_cb(&a, menu_slide_cb);
    lv_anim_set_values(&a, 0, 1024);
    lv_anim_set_duration(&a, SLIDE_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

static void menu_delete_cb(lv_event_t *e)
{
    ui_menu_t *m = lv_event_get_user_data(e);
    lv_anim_delete(m, NULL);
    lv_free(m);
}

static void item_focus_cb(lv_event_t *e)
{
    ui_menu_t *m = lv_event_get_user_data(e);
    lv_obj_t *obj = lv_event_get_target_obj(e);
    /* Building and deleting screens moves group focus too: those events aren't the user's. */
    if (lv_obj_get_screen(obj) != lv_screen_active()) {
        return;
    }
    for (int i = 0; i < m->count; i++) {
        if (m->items[i].obj == obj && i != m->focus) {
            menu_slide_to(m, i);
        }
    }
}

static ui_menu_t *menu_begin(ui_menu_kind_t kind, const char *title, ui_screen_fn_t back)
{
    lv_obj_t *scr = screen_begin(back);
    ui_menu_t *m = lv_malloc_zeroed(sizeof(*m));
    LV_ASSERT_MALLOC(m);
    m->kind = kind;
    m->scr = scr;
    m->app = s_overlay == OVERLAY_NONE ? s_app : NULL;
    snprintf(m->title, sizeof(m->title), "%s", title ? title : "");
    lv_obj_add_event_cb(scr, menu_delete_cb, LV_EVENT_DELETE, m);
    s_menu = m;
    return m;
}

ui_menu_t *ui_menu_begin(const char *title, ui_screen_fn_t back)
{
    return menu_begin(UI_MENU_LIST, title, back);
}

ui_menu_t *ui_dialog_begin(const char *title, const char *message, ui_screen_fn_t back)
{
    ui_menu_t *m = menu_begin(UI_MENU_DIALOG, title, back);
    snprintf(m->message, sizeof(m->message), "%s", message ? message : "");
    return m;
}

lv_obj_t *ui_menu_add_value(ui_menu_t *m, const char *text, const char *value, lv_event_cb_t cb, intptr_t arg)
{
    if (m->count >= UI_MENU_MAX_ITEMS) {
        ESP_LOGE(TAG, "too many items in %s", m->title);
        return NULL;
    }
    ui_item_t *item = &m->items[m->count++];
    snprintf(item->text, sizeof(item->text), "%s", text ? text : "");
    snprintf(item->value, sizeof(item->value), "%s", value ? value : "");
    item->obj = lv_obj_create(m->scr);
    lv_obj_remove_style_all(item->obj);
    lv_obj_remove_flag(item->obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_group_add_obj(lv_group_get_default(), item->obj);
    lv_obj_add_event_cb(item->obj, item_key_cb, LV_EVENT_KEY, NULL);
    lv_obj_add_event_cb(item->obj, item_focus_cb, LV_EVENT_FOCUSED, m);
    if (cb) {
        lv_obj_add_event_cb(item->obj, cb, LV_EVENT_PRESSED, (void *)arg);
    }
    return item->obj;
}

lv_obj_t *ui_menu_add(ui_menu_t *m, const char *text, lv_event_cb_t cb, intptr_t arg)
{
    return ui_menu_add_value(m, text, NULL, cb, arg);
}

static ui_item_t *menu_item(ui_menu_t *m, lv_obj_t *obj)
{
    for (int i = 0; i < m->count; i++) {
        if (m->items[i].obj == obj) {
            return &m->items[i];
        }
    }
    return NULL;
}

void ui_menu_mark(ui_menu_t *m, lv_obj_t *obj)
{
    ui_item_t *item = menu_item(m, obj);
    if (item) {
        item->marked = true;
    }
}

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
            s_theme->hold_progress(hold->menu, hold->item, 100);
            hold->cb();
        } else {
            s_theme->hold_progress(hold->menu, hold->item, (int)(elapsed * 100 / hold->duration_ms));
        }
        break;
    }
    case LV_EVENT_RELEASED:
        s_theme->hold_progress(hold->menu, hold->item, 0);
        break;
    case LV_EVENT_DELETE:
        lv_free(hold);
        break;
    default:
        break;
    }
}

lv_obj_t *ui_hold_add(ui_menu_t *m, const char *text, uint32_t duration_ms, ui_screen_fn_t cb)
{
    lv_obj_t *obj = ui_menu_add(m, text, NULL, 0);
    if (!obj) {
        return NULL;
    }
    hold_state_t *hold = lv_malloc_zeroed(sizeof(*hold));
    LV_ASSERT_MALLOC(hold);
    hold->menu = m;
    hold->item = menu_item(m, obj);
    hold->item->hold = true;
    hold->duration_ms = duration_ms;
    hold->cb = cb;
    lv_obj_add_event_cb(obj, hold_event_cb, LV_EVENT_ALL, hold);
    return obj;
}

void ui_menu_end(ui_menu_t *m, lv_obj_t *focus)
{
    lv_obj_t *target = focus;
    if (m->count == 0) {
        target = focus_target(m->scr);
        lv_obj_add_event_cb(target, item_key_cb, LV_EVENT_KEY, NULL);
    } else if (!target) {
        target = m->items[0].obj;
    }
    ui_item_t *item = menu_item(m, target);
    m->focus = item ? (int)(item - m->items) : 0;
    m->focus_target = (float)m->focus;
    m->pos = (float)m->focus;
    s_theme->menu_build(m);
    s_theme->menu_place(m);
    for (int i = 0; i < m->count; i++) {
        ui_scroll_activate(m->items[i].obj, i == m->focus);
    }
    screen_end(m->scr, target);
}

void ui_submenu_cb(lv_event_t *e)
{
    ui_navigate((ui_screen_fn_t)lv_event_get_user_data(e));
}

void ui_text_show(const char *title, const char *text, ui_screen_fn_t back)
{
    lv_obj_t *scr = screen_begin(back);
    s_theme->text_build(scr, s_overlay == OVERLAY_NONE ? s_app : NULL, title, text);
    lv_obj_t *target = focus_target(scr);
    lv_obj_add_event_cb(target, item_key_cb, LV_EVENT_KEY, NULL);
    screen_end(scr, target);
}

/* ---- launcher ---------------------------------------------------------------------------- */

static bool profile_locked(void)
{
    return s_profile_active == UI_PROFILE_PHANTOM && s_phantom_state != UI_PHANTOM_OPEN;
}

static void status_fill(ui_status_t *s)
{
    s->profile = ui_profile_name(s_profile_active);
    s->locked = profile_locked();
    s->ble = s_ble_state;
    s->usb = s_usb_mounted;
}

static void update_status(void)
{
    if (s_launcher.scr && s_launcher.scr == lv_screen_active()) {
        status_fill(&s_launcher.status);
        s_theme->status_update(&s_launcher);
    }
}

/* Keeps the launcher's Bluetooth symbol in step with the status LED. */
static uint32_t s_ble_shown;

static void status_timer_cb(lv_timer_t *timer)
{
    if (!s_screen_on || !s_launcher.scr || s_launcher.scr != lv_screen_active()) {
        return;
    }
    uint32_t color = ui_ble_color(&s_launcher.status);
    if (color != s_ble_shown) {
        s_ble_shown = color;
        s_theme->status_update(&s_launcher);
    }
}

static void open_selected_app(void)
{
    s_app = s_apps[s_launcher_index];
    ESP_LOGI(TAG, "starting %s", s_app->name);
    if (s_app->start) {
        s_app->start();
    }
    if (s_app->menu) {
        ui_nav_open(s_app->menu);
    } else {
        s_app->show();
    }
}

static float s_launcher_from;
static float s_launcher_to;

static void launcher_slide_cb(void *var, int32_t v)
{
    s_launcher.pos = s_launcher_from + (s_launcher_to - s_launcher_from) * v / 1024.0f;
    s_theme->launcher_place(&s_launcher);
}

static void launcher_slide(int delta)
{
    s_launcher_from = s_launcher.pos;
    s_launcher_to += delta;
    lv_anim_delete(&s_launcher, NULL);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, &s_launcher);
    lv_anim_set_exec_cb(&a, launcher_slide_cb);
    lv_anim_set_values(&a, 0, 1024);
    lv_anim_set_duration(&a, SLIDE_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

/*
 * The carousel keeps one invisible focusable object per app: the group's next/prev handling then
 * moves between apps exactly like menu items, and focus changes slide the carousel.
 */
static void carousel_event_cb(lv_event_t *e)
{
    size_t index = (size_t)(uintptr_t)lv_event_get_user_data(e);
    if (lv_obj_get_screen(lv_event_get_target_obj(e)) != lv_screen_active()) {
        return;
    }
    switch (lv_event_get_code(e)) {
    case LV_EVENT_FOCUSED:
        if (index != s_launcher_index) {
            int delta = (int)lroundf(ui_wrap((float)index - (float)s_launcher_index, APP_COUNT));
            s_launcher_index = index;
            launcher_slide(delta);
        }
        break;
    case LV_EVENT_PRESSED:
        s_launcher_index = index;
        ui_navigate(open_selected_app);
        break;
    default:
        break;
    }
}

/* Double click on the launcher turns the picture around: the dongle may sit either way up. */
static void launcher_key_cb(lv_event_t *e)
{
    if (lv_event_get_key(e) != LV_KEY_ESC) {
        return;
    }
    settings_t settings;
    settings_get(&settings);
    bool flipped = !settings.screen_flipped;
    if (bsp_display_flip(flipped) == ESP_OK) {
        ui_emit(UI_ACTION_FLIP_SCREEN, flipped);
    }
}

static void show_launcher(void)
{
    lv_obj_t *scr = screen_begin(NULL);
    lv_obj_t *focus = NULL;
    for (size_t i = 0; i < APP_COUNT; i++) {
        lv_obj_t *slot = focus_target(scr);
        lv_obj_add_event_cb(slot, carousel_event_cb, LV_EVENT_ALL, (void *)(uintptr_t)i);
        lv_obj_add_event_cb(slot, launcher_key_cb, LV_EVENT_KEY, NULL);
        if (i == s_launcher_index) {
            focus = slot;
        }
    }
    memset(&s_launcher, 0, sizeof(s_launcher));
    s_launcher.scr = scr;
    s_launcher.apps = s_apps;
    s_launcher.count = APP_COUNT;
    s_launcher.pos = s_launcher_to = (float)s_launcher_index;
    status_fill(&s_launcher.status);
    s_theme->launcher_build(&s_launcher);
    s_theme->launcher_place(&s_launcher);
    s_ble_shown = ui_ble_color(&s_launcher.status);
    screen_end(scr, focus);
}

bool ui_app_info(size_t index, const char **id, const char **name)
{
    if (index >= APP_COUNT) {
        return false;
    }
    *id = s_apps[index]->id;
    *name = s_apps[index]->name;
    return true;
}

/* The running app's root screen, or the launcher when no app runs. */
static void show_home(void)
{
    if (s_app && s_app->menu) {
        ui_nav_show();
    } else if (s_app) {
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

/* ---- overlays ---------------------------------------------------------------------------- */

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

/* An item answering the overlay: hold, or double click, emits action. */
static lv_obj_t *overlay_item(ui_menu_t *m, const char *text, ui_action_t action)
{
    lv_obj_t *item = ui_menu_add(m, text, overlay_click_cb, action);
    lv_obj_remove_event_cb(item, item_key_cb);
    lv_obj_add_event_cb(item, overlay_key_cb, LV_EVENT_KEY, (void *)(intptr_t)action);
    return item;
}

static void fido_approve(void)
{
    ui_emit(UI_ACTION_FIDO_CONFIRM, 0);
}

static void fido_reset_default(void)
{
    ui_emit(UI_ACTION_FIDO_RESET_CONFIRM, UI_PROFILE_MAIN);
}

static void fido_reset_phantom(void)
{
    ui_emit(UI_ACTION_FIDO_RESET_CONFIRM, UI_PROFILE_PHANTOM);
}

/* A security key reset erases one profile's credentials: a hold item per profile picks it. */
static void show_fido_reset(void)
{
    static const ui_screen_fn_t confirm[UI_PROFILE_COUNT] = {
        [UI_PROFILE_MAIN] = fido_reset_default,
        [UI_PROFILE_PHANTOM] = fido_reset_phantom,
    };
    ui_menu_t *m = ui_menu_begin("Erase security key?", NULL);
    lv_obj_t *focus = NULL;
    for (int i = 0; i < UI_PROFILE_COUNT; i++) {
        char text[32];
        snprintf(text, sizeof(text), "Hold: %s", ui_profile_name(i));
        lv_obj_t *hold = ui_hold_add(m, text, UI_HOLD_CONFIRM_MS, confirm[i]);
        lv_obj_remove_event_cb(hold, item_key_cb);
        if (i == (int)s_profile_active) {
            focus = hold;
        }
    }
    ui_menu_end(m, focus);
}

/* Double click must not approve (or leave) here: only the hold item answers the request. */
static void show_fido(void)
{
    if (s_fido_kind == UI_FIDO_RESET) {
        show_fido_reset();
        return;
    }
    static const char *const titles[] = {
        [UI_FIDO_OTHER] = "Security key",
        [UI_FIDO_REGISTER] = "Register on",
        [UI_FIDO_SIGN_IN] = "Sign in to",
        [UI_FIDO_SELECT] = "Use this key?",
        [UI_FIDO_RESET] = "Erase security key?",
        [UI_FIDO_PGP_SIGN] = "Sign with key?",
        [UI_FIDO_PGP_DECRYPT] = "Decrypt with key?",
        [UI_FIDO_PGP_AUTH] = "Log in with key?",
        [UI_FIDO_PROFILE_RESET] = "Erase profile?",
    };
    static const char *const subtitles[] = {
        [UI_FIDO_PGP_SIGN] = "OpenPGP",
        [UI_FIDO_PGP_DECRYPT] = "OpenPGP",
        [UI_FIDO_PGP_AUTH] = "SSH / OpenPGP",
    };
    bool named = s_fido_kind == UI_FIDO_REGISTER || s_fido_kind == UI_FIDO_SIGN_IN || s_fido_kind == UI_FIDO_PROFILE_RESET;
    const char *subtitle = named ? s_fido_rp_id
                           : s_fido_kind < sizeof(subtitles) / sizeof(subtitles[0]) ? subtitles[s_fido_kind]
                                                                                   : NULL;
    ui_menu_t *m = menu_begin(UI_MENU_PROMPT, titles[s_fido_kind], NULL);
    snprintf(m->subtitle, sizeof(m->subtitle), "%s", subtitle ? subtitle : "");
    if (s_fido_kind == UI_FIDO_REGISTER || s_fido_kind == UI_FIDO_SIGN_IN) {
        m->site = ui_site_find(s_fido_rp_id);
    }
    bool erase = s_fido_kind == UI_FIDO_PROFILE_RESET;
    m->tone = erase ? UI_TONE_BAD : UI_TONE_GOOD;
    lv_obj_t *hold = ui_hold_add(m, erase ? "Hold to erase" : "Hold to approve",
                                 erase ? UI_HOLD_RESET_MS : UI_HOLD_CONFIRM_MS, fido_approve);
    lv_obj_remove_event_cb(hold, item_key_cb);
    ui_menu_end(m, hold);
}

static void fido_result_timer_cb(lv_timer_t *timer)
{
    s_fido_result_timer = NULL;
    s_fido_result_shown = false;
    s_fido_locked = false;
    update_overlay();
}

static void fido_result_show_for(uint32_t ms)
{
    s_fido_result_shown = true;
    if (s_fido_result_timer) {
        lv_timer_set_period(s_fido_result_timer, ms);
        lv_timer_reset(s_fido_result_timer);
    } else {
        s_fido_result_timer = lv_timer_create(fido_result_timer_cb, ms, NULL);
        lv_timer_set_repeat_count(s_fido_result_timer, 1);
    }
}

static void show_fido_result(void)
{
    ui_menu_t *m = menu_begin(UI_MENU_RESULT, s_fido_locked ? ui_profile_name(s_profile_active) : "Security key", NULL);
    if (s_fido_locked) {
        m->tone = UI_TONE_WARN;
        snprintf(m->big, sizeof(m->big), "Locked");
        snprintf(m->message, sizeof(m->message), "Waiting for phone");
    } else {
        m->tone = s_fido_approved ? UI_TONE_GOOD : UI_TONE_BAD;
        snprintf(m->big, sizeof(m->big), "%s", s_fido_approved ? "Approved" : "Not approved");
    }
    ui_menu_end(m, NULL);
}

/* Shows no counts or text: either would leak password length or content. */
static void show_typing(void)
{
    ui_menu_t *m = ui_dialog_begin("Typing...", "From your phone", NULL);
    ui_menu_end(m, overlay_item(m, "Hold to abort", UI_ACTION_ABORT_TYPING));
}

/* Version 1 (21x21) holds the pairing text at level L; 3 px modules and a 2-module quiet zone fit 80 px. */
#define QR_VERSION      1
#define QR_MODULES      21
#define QR_SCALE        3
#define QR_QUIET        2
#define QR_PX           ((QR_MODULES + 2 * QR_QUIET) * QR_SCALE)

LV_DRAW_BUF_DEFINE_STATIC(s_qr_buf, QR_PX, QR_PX, LV_COLOR_FORMAT_I1);

static lv_obj_t *qr_create(lv_obj_t *parent, const char *text)
{
    uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(QR_VERSION)];
    uint8_t tmp[qrcodegen_BUFFER_LEN_FOR_VERSION(QR_VERSION)];
    if (!qrcodegen_encodeText(text, tmp, qr, qrcodegen_Ecc_LOW, QR_VERSION, QR_VERSION, qrcodegen_Mask_AUTO,
                              false)) {
        ESP_LOGE(TAG, "pairing code does not fit a version %d QR code", QR_VERSION);
        return NULL;
    }
    lv_image_cache_drop(&s_qr_buf);
    LV_DRAW_BUF_INIT_STATIC(s_qr_buf);
    lv_obj_t *canvas = lv_canvas_create(parent);
    lv_canvas_set_draw_buf(canvas, &s_qr_buf);
    lv_canvas_set_palette(canvas, 0, lv_color_to_32(lv_color_white(), LV_OPA_COVER));
    lv_canvas_set_palette(canvas, 1, lv_color_to_32(lv_color_black(), LV_OPA_COVER));

    /* I1 pixels after the 2-entry palette, most significant bit first; 1 selects black. */
    uint8_t *px = s_qr_buf.data + 2 * sizeof(lv_color32_t);
    uint32_t stride = s_qr_buf.header.stride;
    memset(px, 0, stride * QR_PX);
    for (int y = 0; y < QR_PX; y++) {
        for (int x = 0; x < QR_PX; x++) {
            int mx = x / QR_SCALE - QR_QUIET;
            int my = y / QR_SCALE - QR_QUIET;
            if (qrcodegen_getModule(qr, mx, my)) {
                px[y * stride + x / 8] |= 0x80 >> (x % 8);
            }
        }
    }
    return canvas;
}

/* QR code for the hub app, digits for typing into the phone's own pairing dialog. */
static void show_pairing(void)
{
    settings_t settings;
    settings_get(&settings);
    const ui_palette_t *pal = &s_theme->pal;

    lv_obj_t *scr = screen_begin(NULL);
    lv_obj_t *qr = qr_create(scr, s_pairing_qr);
    if (qr) {
        lv_obj_set_pos(qr, 0, (UI_H - QR_PX) / 2);
    }
    const int x = QR_PX + 2, w = UI_W - x - 2;
    lv_obj_t *name = ui_label_scroll(scr, &lv_font_montserrat_12, pal->dim, settings.ble_name, w);
    ui_label_scroll_align(name, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_pos(name, x, 6);
    for (int half = 0; half < 2; half++) {
        char digits[8];
        snprintf(digits, sizeof(digits), "%03lu",
                 (unsigned long)(half ? s_pairing_passkey % 1000 : s_pairing_passkey / 1000));
        lv_obj_t *l = ui_label(scr, &lv_font_montserrat_24, pal->text, digits);
        lv_obj_set_width(l, w);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(l, x, 22 + half * 26);
    }

    /* Hold or double click cancels, as on the other overlays. */
    lv_obj_t *cancel = focus_target(scr);
    lv_obj_add_event_cb(cancel, overlay_click_cb, LV_EVENT_PRESSED, (void *)(intptr_t)UI_ACTION_CANCEL_PAIRING);
    lv_obj_add_event_cb(cancel, overlay_key_cb, LV_EVENT_KEY, (void *)(intptr_t)UI_ACTION_CANCEL_PAIRING);
    screen_end(scr, cancel);
}

static void show_passkey(void)
{
    ui_menu_t *m = ui_dialog_begin("Enter on phone", NULL, NULL);
    snprintf(m->big, sizeof(m->big), "%03lu %03lu", (unsigned long)(s_passkey / 1000), (unsigned long)(s_passkey % 1000));
    ui_menu_end(m, overlay_item(m, "Hold to cancel", UI_ACTION_CANCEL_PAIRING));
}

static void set_update_label(void)
{
    if (s_update_label) {
        char text[8];
        snprintf(text, sizeof(text), "%u%%", s_update_percent);
        ui_label_set_text(s_update_label, text);
    }
}

static void show_update(void)
{
    ui_menu_t *m = menu_begin(UI_MENU_RESULT, "Updating firmware", NULL);
    snprintf(m->big, sizeof(m->big), "%u%%", s_update_percent);
    ui_menu_end(m, NULL);
    s_update_label = m->big_label;
}

static const ui_screen_fn_t s_overlay_screens[] = {
    [OVERLAY_NONE] = show_home,
    [OVERLAY_PAIRING] = show_pairing,
    [OVERLAY_TYPING] = show_typing,
    [OVERLAY_FIDO_RESULT] = show_fido_result,
    [OVERLAY_FIDO] = show_fido,
    [OVERLAY_PASSKEY] = show_passkey,
    [OVERLAY_UPDATE] = show_update,
};

static void update_overlay(void)
{
    overlay_t want = s_updating            ? OVERLAY_UPDATE
                     : s_passkey_shown     ? OVERLAY_PASSKEY
                     : s_fido_waiting      ? OVERLAY_FIDO
                     : s_fido_result_shown ? OVERLAY_FIDO_RESULT
                     : s_typing            ? OVERLAY_TYPING
                     : s_pairing           ? OVERLAY_PAIRING
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
    navigate(s_overlay_screens[want]);
}

/* ---- public ------------------------------------------------------------------------------ */

void ui_reload_theme(void)
{
    if (!lvgl_port_lock(PROMPT_LOCK_MS)) {
        return;
    }
    theme_load();
    navigate(s_overlay_screens[s_overlay]);
    lvgl_port_unlock();
}

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

#define FIDO_RESULT_MS      1500

void ui_show_fido_request(ui_fido_request_t kind, const char *rp_id)
{
    if (!lvgl_port_lock(PROMPT_LOCK_MS)) {
        return;
    }
    s_fido_kind = kind;
    snprintf(s_fido_rp_id, sizeof(s_fido_rp_id), "%s", rp_id ? rp_id : "");
    s_fido_waiting = true;
    s_fido_result_shown = false;
    update_overlay();
    lvgl_port_unlock();
}

void ui_show_fido_done(bool approved)
{
    if (!lvgl_port_lock(PROMPT_LOCK_MS)) {
        return;
    }
    s_fido_waiting = false;
    s_fido_approved = approved;
    s_fido_locked = false;
    fido_result_show_for(FIDO_RESULT_MS);
    update_overlay();
    lvgl_port_unlock();
}

#define LOCKED_NOTICE_MS    2500

void ui_show_profile_locked(void)
{
    if (!lvgl_port_lock(PROMPT_LOCK_MS)) {
        return;
    }
    if (!s_fido_waiting) {
        bool shown = s_fido_result_shown && s_fido_locked;
        s_fido_locked = true;
        fido_result_show_for(LOCKED_NOTICE_MS);
        if (!shown && s_overlay == OVERLAY_FIDO_RESULT) {
            navigate(show_fido_result);
        }
        update_overlay();
    }
    lvgl_port_unlock();
}

const char *ui_profile_name(ui_profile_t profile)
{
    return profile == UI_PROFILE_PHANTOM ? "Phantom" : "Main";
}

ui_profile_t ui_profile_active(void)
{
    return s_profile_active;
}

ui_profile_t ui_profile_default(void)
{
    return s_profile_default;
}

ui_phantom_state_t ui_phantom_state(void)
{
    return s_phantom_state;
}

void ui_set_profiles(ui_profile_t active, ui_profile_t default_profile, ui_phantom_state_t phantom_state)
{
    if (!lvgl_port_lock(PROMPT_LOCK_MS)) {
        return;
    }
    s_profile_active = active;
    s_profile_default = default_profile;
    s_phantom_state = phantom_state;
    update_status();
    if (s_app && s_app->menu && s_overlay == OVERLAY_NONE) {
        navigate(ui_nav_show);
    } else if (s_app && s_app->refresh && s_overlay == OVERLAY_NONE) {
        navigate(s_app->refresh);
    }
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

void ui_show_pairing(bool open, uint32_t passkey, const char *qr_text)
{
    if (!lvgl_port_lock(PROMPT_LOCK_MS)) {
        return;
    }
    bool changed = passkey != s_pairing_passkey || strcmp(qr_text, s_pairing_qr) != 0;
    s_pairing = open;
    s_pairing_passkey = passkey;
    snprintf(s_pairing_qr, sizeof(s_pairing_qr), "%s", qr_text);
    update_overlay();
    if (changed && s_overlay == OVERLAY_PAIRING) {
        navigate(show_pairing);
    }
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
    if (!lvgl_port_lock(PROMPT_LOCK_MS)) {
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
    /* Themes style everything they draw: LVGL's default theme would only add unused styles. */
    lv_display_set_theme(lv_display_get_default(), NULL);
    theme_load();
    settings_t settings;
    settings_get(&settings);
    if (settings.screen_flipped) {
        bsp_display_flip(true);
    }

    esp_err_t ret = ESP_OK;
    s_indev = ui_input_create(btn);
    if (s_indev) {
        show_launcher();
        lv_timer_create(screen_timer_cb, SCREEN_TICK_MS, NULL);
        lv_timer_create(status_timer_cb, STATUS_TICK_MS, NULL);
        screen_set(false);
    } else {
        ret = ESP_ERR_NO_MEM;
    }

    lvgl_port_unlock();
    ESP_LOGI(TAG, "started");
    return ret;
}

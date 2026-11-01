#include "ui_priv.h"
#include "usb_hid.h"

#define JIGGLE_INTERVAL_S   2
#define JIGGLE_INTERVAL_MS  (JIGGLE_INTERVAL_S * 1000)
#define STR_(x)             #x
#define STR(x)              STR_(x)
#define JIGGLE_DISTANCE     60

static lv_timer_t *s_timer;
static int8_t s_direction = 1;

/* Alternates right and left so the cursor visibly moves without drifting away. */
static void jiggle_cb(lv_timer_t *timer)
{
    if (ui_host_busy()) {
        return;
    }
    if (usb_hid_mouse_report(0, s_direction * JIGGLE_DISTANCE, 0, 0, 0) == ESP_OK) {
        s_direction = -s_direction;
    }
}

static void start(void)
{
    s_direction = 1;
    s_timer = lv_timer_create(jiggle_cb, JIGGLE_INTERVAL_MS, NULL);
}

static void stop(void)
{
    lv_timer_delete(s_timer);
    s_timer = NULL;
    if (s_direction < 0) {
        usb_hid_mouse_report(0, -JIGGLE_DISTANCE, 0, 0, 0);
    }
}

static const ui_option_t s_options[] = {
    UI_EXIT("Hold to stop"),
};

/* Runs until stopped with a hold: a stray double click only turns the screen off. */
static const ui_menu_def_t s_menu = {
    .title = "Mouse jiggler",
    .message = "Moving every " STR(JIGGLE_INTERVAL_S) " s",
    .options = s_options,
    .count = sizeof(s_options) / sizeof(s_options[0]),
    .exit_by_hold = true,
};

const ui_app_t ui_app_jiggler = {
    .id = "jiggler",
    .name = "Jiggler",
    .icon = LV_SYMBOL_SHUFFLE,
    .color = 0xFF6B6B,
    .start = start,
    .menu = &s_menu,
    .stop = stop,
};

#include "ui_priv.h"
#include "usb_hid.h"

#define JIGGLE_INTERVAL_MS  2000
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

static void show(void)
{
    lv_obj_t *list = ui_menu_begin("Mouse jiggler", NULL);
    lv_obj_t *info = lv_label_create(list);
    lv_label_set_text_fmt(info, "Moving every %u s", JIGGLE_INTERVAL_MS / 1000);
    ui_menu_end(list, ui_hold_add(list, "Hold to stop", UI_HOLD_EXIT_MS, ui_app_exit));
}

const ui_app_t ui_app_jiggler = {
    .name = "Jiggler",
    .icon = LV_SYMBOL_SHUFFLE,
    .start = start,
    .show = show,
    .stop = stop,
};

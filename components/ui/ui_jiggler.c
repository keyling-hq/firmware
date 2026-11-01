#include <stdio.h>
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
    char message[32];
    snprintf(message, sizeof(message), "Moving every %u s", JIGGLE_INTERVAL_MS / 1000);
    ui_menu_t *dialog = ui_dialog_begin("Mouse jiggler", message, NULL);
    ui_menu_end(dialog, ui_hold_add(dialog, "Hold to stop", UI_HOLD_EXIT_MS, ui_app_exit));
}

const ui_app_t ui_app_jiggler = {
    .id = "jiggler",
    .name = "Jiggler",
    .icon = LV_SYMBOL_SHUFFLE,
    .color = 0xFF6B6B,
    .start = start,
    .show = show,
    .stop = stop,
};

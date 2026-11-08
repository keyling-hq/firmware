#include "ui_priv.h"
#include "ui_input.h"
#include "status_led.h"
#include "usb_hid.h"

#define POLL_MS         250
#define EJECTED_SHOW_MS 1000

static lv_timer_t *s_timer;
static bool s_ejected;

static void leave_cb(lv_timer_t *timer)
{
    ui_app_exit();
}

/* Once the computer ejects the drive there is nothing left to do here: the app leaves by itself. */
static void poll_cb(lv_timer_t *timer)
{
    if (!usb_hid_disk_ejected()) {
        return;
    }
    s_ejected = true;
    status_led_drive(false);
    if (!ui_screen_is_on()) {
        ui_app_exit();
        return;
    }
    lv_timer_set_cb(timer, leave_cb);
    lv_timer_set_period(timer, EJECTED_SHOW_MS);
    lv_timer_reset(timer);
    ui_refresh();
}

static void start(void)
{
    s_ejected = false;
    ui_emit(UI_ACTION_SHOW_DRIVE, 1);
    s_timer = lv_timer_create(poll_cb, POLL_MS, NULL);
}

static void stop(void)
{
    lv_timer_delete(s_timer);
    s_timer = NULL;
    ui_emit(UI_ACTION_SHOW_DRIVE, 0);
}

static const char *title(intptr_t arg)
{
    return s_ejected ? "Drive ejected" : "USB drive";
}

static const ui_option_t s_options[] = {
    UI_EXIT("Hold to stop"),
};

/* The smartcard is away while the drive shows; it comes back when the app is left. */
static const ui_menu_def_t s_menu = {
    .title_fn = title,
    .message = "KEYLING on the computer, read only",
    .options = s_options,
    .count = sizeof(s_options) / sizeof(s_options[0]),
    .exit_by_hold = true,
};

const ui_app_t ui_app_drive = {
    .id = "drive",
    .name = "Drive",
    .icon = LV_SYMBOL_DRIVE,
    .start = start,
    .menu = &s_menu,
    .stop = stop,
};

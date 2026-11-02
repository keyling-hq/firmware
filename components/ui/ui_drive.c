#include "ui_priv.h"
#include "usb_hid.h"

#define POLL_MS 250

static lv_timer_t *s_timer;
static bool s_ejected;

static void poll_cb(lv_timer_t *timer)
{
    bool ejected = usb_hid_disk_ejected();
    if (ejected != s_ejected) {
        s_ejected = ejected;
        ui_refresh();
    }
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
    .color = 0x5BC0EB,
    .start = start,
    .menu = &s_menu,
    .stop = stop,
};

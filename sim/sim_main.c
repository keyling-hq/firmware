/*
 * Keyling UI simulator: runs components/ui on the host.
 *
 *   keyling_ui_sim                         window, the space bar is the button
 *   keyling_ui_sim --out DIR SCRIPT...     headless: runs the scripts, saves PNGs into DIR
 *   --zoom N                               window scale (default 4)
 *
 * Script commands, one per line or separated by ';' ('#' starts a comment):
 *   wait MS | click | double | hold MS | press | release | shot NAME
 *   usb on|off | ble advertising|unpaired|pairing|securing|connected | typing on|off
 *   pairing PASSKEY|off | passkey PASSKEY|off | update PERCENT|off
 *   fido other|register|sign_in|select|reset|pgp_sign|pgp_decrypt|pgp_auth|profile_reset [RP_ID]
 *   fido_done ok|fail | profiles main|phantom [main|phantom] [open|new|waiting] | locked
 *   theme ID (candy, device, quiet with SIM_EXAMPLES) | zoom N (PNG scale, default 4)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lvgl.h"
#include "keymap.h"
#include "settings.h"
#include "ui.h"
#include "sim.h"

#define WIDTH   160
#define HEIGHT  80
#define STEP_MS 5

static bool s_headless;
static uint32_t s_now;
static uint16_t s_fb[WIDTH * HEIGHT];
static const char *s_out = ".";
static int s_zoom = 4;

static ui_profile_t s_active = UI_PROFILE_MAIN, s_default = UI_PROFILE_MAIN;
static ui_phantom_state_t s_phantom = UI_PHANTOM_OPEN;

/* Actions are applied between LVGL runs, as the firmware applies them from its own tasks. */
#define MAX_PENDING 8
static struct {
    ui_action_t action;
    int arg;
} s_pending[MAX_PENDING];
static int s_pending_count;

static uint32_t tick_cb(void)
{
    return s_headless ? s_now : sim_clock_ms();
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px)
{
    lv_display_flush_ready(disp);
}

static void action_cb(ui_action_t action, int arg)
{
    printf("action %d arg %d\n", action, arg);
    if (s_pending_count < MAX_PENDING) {
        s_pending[s_pending_count].action = action;
        s_pending[s_pending_count].arg = arg;
        s_pending_count++;
    }
}

static void apply_action(ui_action_t action, int arg)
{
    settings_t settings;
    settings_get(&settings);
    switch (action) {
    case UI_ACTION_SET_LAYOUT:
        strncpy(settings.layout, keymap_layout_get(arg)->id, sizeof(settings.layout) - 1);
        settings_set(&settings);
        break;
    case UI_ACTION_SET_SPEED:
        settings.key_press_ms = settings_speed_presets[arg].key_press_ms;
        settings.key_release_ms = settings_speed_presets[arg].key_release_ms;
        settings_set(&settings);
        break;
    case UI_ACTION_PAIR_NEW:
        ui_set_ble_state(UI_BLE_UNPAIRED);
        ui_show_pairing(true, 482915, "KL1:34851857FE92:482915");
        break;
    case UI_ACTION_CANCEL_PAIRING:
        ui_show_pairing(false, 0, NULL);
        break;
    case UI_ACTION_FORGET_PHONE:
        ui_set_ble_state(UI_BLE_UNPAIRED);
        break;
    case UI_ACTION_FIDO_CONFIRM:
    case UI_ACTION_FIDO_RESET_CONFIRM:
        ui_show_fido_done(true);
        break;
    case UI_ACTION_ABORT_TYPING:
        ui_show_typing(false);
        break;
    case UI_ACTION_SELECT_PROFILE:
        s_active = arg;
        ui_set_profiles(s_active, s_default, s_phantom);
        break;
    case UI_ACTION_DEFAULT_PROFILE:
        s_active = s_default = arg;
        ui_set_profiles(s_active, s_default, s_phantom);
        break;
    case UI_ACTION_SET_THEME:
        strncpy(settings.theme, ui_theme_id(arg), sizeof(settings.theme) - 1);
        settings_set(&settings);
        break;
    case UI_ACTION_RESET_PROFILE:
        if (arg == UI_PROFILE_PHANTOM) {
            s_phantom = UI_PHANTOM_NEW;
            ui_set_profiles(s_active, s_default, s_phantom);
        }
        break;
    default:
        break;
    }
}

/* As the phone would change it: through settings, then the UI reloads it. */
static void set_theme(const char *id)
{
    settings_t settings;
    settings_get(&settings);
    snprintf(settings.theme, sizeof(settings.theme), "%s", id);
    settings_set(&settings);
    ui_reload_theme();
}

static void run_once(void)
{
    lv_timer_handler();
    int count = s_pending_count;
    s_pending_count = 0;
    for (int i = 0; i < count; i++) {
        apply_action(s_pending[i].action, s_pending[i].arg);
    }
}

static void advance(uint32_t ms)
{
    for (uint32_t t = 0; t < ms; t += STEP_MS) {
        s_now += STEP_MS;
        sim_button_tick(s_now);
        run_once();
    }
}

static void press(bool down)
{
    sim_button_set(down, s_now);
    run_once();
}

static void shot(const char *name)
{
    lv_refr_now(NULL);
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    printf("%s: LVGL heap %u used, %u at most, of %u\n", name, (unsigned)(mon.total_size - mon.free_size),
           (unsigned)mon.max_used, (unsigned)mon.total_size);
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.png", s_out, name);
    if (!sim_png_write(path, s_fb, WIDTH, HEIGHT, s_zoom, sim_brightness() == 0)) {
        fprintf(stderr, "cannot write %s\n", path);
        exit(1);
    }
    printf("wrote %s\n", path);
}

static const char *const s_ble_names[] = { "advertising", "unpaired", "pairing", "securing", "connected" };
static const char *const s_fido_names[] = { "other", "register", "sign_in", "select", "reset",
                                            "pgp_sign", "pgp_decrypt", "pgp_auth", "profile_reset" };

static int lookup(const char *word, const char *const *names, int count)
{
    for (int i = 0; word && i < count; i++) {
        if (strcmp(word, names[i]) == 0) {
            return i;
        }
    }
    return -1;
}

static bool on(const char *word)
{
    return word && strcmp(word, "off") != 0;
}

static ui_profile_t profile_arg(const char *word, ui_profile_t fallback)
{
    return !word ? fallback : strcmp(word, "phantom") == 0 ? UI_PROFILE_PHANTOM : UI_PROFILE_MAIN;
}

static void command(char *line)
{
    char *argv[4] = { 0 };
    int argc = 0;
    for (char *tok = strtok(line, " \t\r"); tok && argc < 4; tok = strtok(NULL, " \t\r")) {
        argv[argc++] = tok;
    }
    if (argc == 0) {
        return;
    }
    const char *cmd = argv[0], *a1 = argv[1], *a2 = argv[2], *a3 = argv[3];
    if (strcmp(cmd, "wait") == 0) {
        advance(a1 ? atoi(a1) : 0);
    } else if (strcmp(cmd, "click") == 0) {
        press(true);
        advance(60);
        press(false);
        advance(250);
    } else if (strcmp(cmd, "double") == 0) {
        press(true);
        advance(60);
        press(false);
        advance(80);
        press(true);
        advance(60);
        press(false);
        advance(250);
    } else if (strcmp(cmd, "hold") == 0) {
        press(true);
        advance(a1 ? atoi(a1) : 1000);
        press(false);
        advance(250);
    } else if (strcmp(cmd, "press") == 0) {
        press(true);
    } else if (strcmp(cmd, "release") == 0) {
        press(false);
    } else if (strcmp(cmd, "shot") == 0 && a1) {
        shot(a1);
    } else if (strcmp(cmd, "zoom") == 0 && a1) {
        s_zoom = atoi(a1) > 0 ? atoi(a1) : 1;
    } else if (strcmp(cmd, "usb") == 0) {
        ui_set_usb_state(on(a1));
    } else if (strcmp(cmd, "ble") == 0 && lookup(a1, s_ble_names, 5) >= 0) {
        ui_set_ble_state(lookup(a1, s_ble_names, 5));
    } else if (strcmp(cmd, "typing") == 0) {
        ui_show_typing(on(a1));
    } else if (strcmp(cmd, "pairing") == 0) {
        uint32_t passkey = on(a1) ? (uint32_t)atoi(a1) : 0;
        char qr[UI_PAIRING_QR_MAX];
        snprintf(qr, sizeof(qr), "KL1:34851857FE92:%06u", (unsigned)passkey);
        ui_show_pairing(on(a1), passkey, qr);
    } else if (strcmp(cmd, "passkey") == 0) {
        ui_show_passkey(on(a1) ? (uint32_t)atoi(a1) : 0, on(a1));
    } else if (strcmp(cmd, "update") == 0) {
        ui_show_update(on(a1), on(a1) ? atoi(a1) : 0);
    } else if (strcmp(cmd, "fido") == 0 && lookup(a1, s_fido_names, 9) >= 0) {
        ui_show_fido_request(lookup(a1, s_fido_names, 9), a2 ? a2 : "");
    } else if (strcmp(cmd, "fido_done") == 0) {
        ui_show_fido_done(a1 && strcmp(a1, "ok") == 0);
    } else if (strcmp(cmd, "profiles") == 0) {
        s_active = profile_arg(a1, s_active);
        s_default = profile_arg(a2, s_default);
        if (a3) {
            s_phantom = strcmp(a3, "new") == 0 ? UI_PHANTOM_NEW
                        : strcmp(a3, "waiting") == 0 ? UI_PHANTOM_WAITING : UI_PHANTOM_OPEN;
        }
        ui_set_profiles(s_active, s_default, s_phantom);
    } else if (strcmp(cmd, "locked") == 0) {
        ui_show_profile_locked();
    } else if (strcmp(cmd, "theme") == 0 && a1) {
        set_theme(a1);
    } else {
        fprintf(stderr, "unknown command: %s\n", cmd);
        exit(2);
    }
    run_once();
}

static void run_script(const char *path)
{
    FILE *f = strcmp(path, "-") == 0 ? stdin : fopen(path, "r");
    if (!f) {
        fprintf(stderr, "cannot open %s\n", path);
        exit(1);
    }
    char buf[256];
    while (fgets(buf, sizeof(buf), f)) {
        char *hash = strchr(buf, '#');
        if (hash) {
            *hash = '\0';
        }
        for (char *save = NULL, *part = strtok_r(buf, ";\n", &save); part; part = strtok_r(NULL, ";\n", &save)) {
            char copy[256];
            strncpy(copy, part, sizeof(copy) - 1);
            copy[sizeof(copy) - 1] = '\0';
            command(copy);
        }
    }
    if (f != stdin) {
        fclose(f);
    }
}

static void start_ui(void)
{
    if (ui_start(sim_button_handle(), action_cb) != ESP_OK) {
        fprintf(stderr, "ui_start failed\n");
        exit(1);
    }
    ui_set_fido_status("ok");
    ui_set_usb_state(true);
    ui_set_ble_state(UI_BLE_CONNECTED);
    ui_set_profiles(s_active, s_default, s_phantom);
}

static bool key_edge(int key, bool *was)
{
    bool down = sim_key_down(key);
    bool edge = down && !*was;
    *was = down;
    return edge;
}

static void run_window(void)
{
    if (!sim_window_create(WIDTH, HEIGHT, s_zoom)) {
        fprintf(stderr, "cannot create the window\n");
        exit(1);
    }
    start_ui();
    printf("space: button | t: typing | p: pairing | f: security key request | x: deny it | u: USB | b: Bluetooth | "
           "l: locked | 1-9: theme | esc: quit\n");

    bool was[8] = { 0 };
    bool theme_was[9] = { 0 };
    bool typing = false, pairing = false, usb = true;
    int ble = UI_BLE_CONNECTED;
    while (sim_window_open()) {
        bool focused = sim_window_focused();
        sim_button_set(focused && sim_key_down(SIM_KEY_SPACE), sim_clock_ms());
        sim_button_tick(sim_clock_ms());
        if (focused) {
            for (int i = 0; i < 9 && ui_theme_id(i); i++) {
                if (key_edge('1' + i, &theme_was[i])) {
                    set_theme(ui_theme_id(i));
                }
            }
            if (key_edge('T', &was[0])) {
                ui_show_typing(typing = !typing);
            }
            if (key_edge('P', &was[1])) {
                pairing = !pairing;
                ui_show_pairing(pairing, 482915, "KL1:34851857FE92:482915");
            }
            if (key_edge('F', &was[2])) {
                ui_show_fido_request(UI_FIDO_SIGN_IN, "github.com");
            }
            if (key_edge('X', &was[7])) {
                ui_show_fido_done(false);
            }
            if (key_edge('U', &was[3])) {
                ui_set_usb_state(usb = !usb);
            }
            if (key_edge('B', &was[4])) {
                ble = (ble + 1) % 5;
                ui_set_ble_state(ble);
            }
            if (key_edge('L', &was[5])) {
                ui_show_profile_locked();
            }
            if (key_edge(SIM_KEY_ESC, &was[6])) {
                break;
            }
        }
        run_once();
        sim_sleep_ms(STEP_MS);
    }
}

int main(int argc, char **argv)
{
    int first_script = argc;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
            s_out = argv[++i];
            s_headless = true;
        } else if (strcmp(argv[i], "--zoom") == 0 && i + 1 < argc) {
            s_zoom = atoi(argv[++i]);
        } else {
            first_script = i;
            s_headless = true;
            break;
        }
    }

    lv_init();
    lv_tick_set_cb(tick_cb);
    if (!s_headless) {
        run_window();
        return 0;
    }

    sim_make_dir(s_out);
    lv_display_t *disp = lv_display_create(WIDTH, HEIGHT);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(disp, s_fb, NULL, sizeof(s_fb), LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(disp, flush_cb);
    start_ui();
    advance(100);
    for (int i = first_script; i < argc; i++) {
        run_script(argv[i]);
    }
    return 0;
}

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "iot_button.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_ACTION_TYPE_TEST,
    UI_ACTION_SET_LAYOUT,       /* arg: keymap layout index */
    UI_ACTION_SET_SPEED,        /* arg: settings speed preset index */
    UI_ACTION_ABORT_TYPING,
    UI_ACTION_PAIR_NEW,
    UI_ACTION_CANCEL_PAIRING,
    UI_ACTION_FORGET_PHONE,
    UI_ACTION_FACTORY_RESET,
    UI_ACTION_FIDO_CONFIRM,
    UI_ACTION_FIDO_RESET_CONFIRM,   /* arg: ui_profile_t whose credentials the reset erases */
    UI_ACTION_LOG_DIAGNOSTICS,
    UI_ACTION_SELECT_PROFILE,       /* arg: ui_profile_t */
    UI_ACTION_DEFAULT_PROFILE,      /* arg: ui_profile_t, also made active */
    UI_ACTION_RESET_PROFILE,        /* arg: ui_profile_t; the user already confirmed */
} ui_action_t;

typedef enum {
    UI_PROFILE_DEFAULT,
    UI_PROFILE_PROTECTED,
    UI_PROFILE_COUNT,
} ui_profile_t;

typedef enum {
    UI_PROTECTED_OPEN,
    UI_PROTECTED_NEW,
    UI_PROTECTED_WAITING,
} ui_protected_state_t;

typedef enum {
    UI_BLE_ADVERTISING,         /* bonded phone not connected */
    UI_BLE_UNPAIRED,
    UI_BLE_PAIRING,
    UI_BLE_SECURING,
    UI_BLE_CONNECTED,
} ui_ble_state_t;

/** Called from the LVGL task. Settings changes must be applied before it returns. */
typedef void (*ui_action_cb_t)(ui_action_t action, int arg);

/** Build the UI and attach the button as LVGL input. Call after bsp_display_start(). */
esp_err_t ui_start(button_handle_t btn, ui_action_cb_t action_cb);

/*
 * Thread safe, callable from any task once ui_start() has returned. The show functions wake
 * the screen; the highest priority active one is displayed: firmware update, passkey, security key
 * confirmation, typing, pairing window.
 */
void ui_show_update(bool active, uint8_t percent);
/** Short status of the security key for the About screen ("ok" or an error name). */
void ui_set_fido_status(const char *status);
typedef enum {
    UI_FIDO_OTHER,
    UI_FIDO_REGISTER,
    UI_FIDO_SIGN_IN,
    UI_FIDO_SELECT,
    UI_FIDO_RESET,
    UI_FIDO_PGP_SIGN,
    UI_FIDO_PGP_DECRYPT,
    UI_FIDO_PGP_AUTH,
    UI_FIDO_PROFILE_RESET,      /* the phone asks to reset the profile named by rp_id */
} ui_fido_request_t;

/** A security key request waits for the user: shows what is asked and a hold-to-approve button. */
void ui_show_fido_request(ui_fido_request_t kind, const char *rp_id);
/** The request was answered (approved) or ended without approval: shows the outcome briefly. */
void ui_show_fido_done(bool approved);

/** The security key profiles: the active one is named on the home screen. */
void ui_set_profiles(ui_profile_t active, ui_profile_t default_profile, ui_protected_state_t protected_state);

/** A request was refused because the active profile is locked: says so briefly. */
void ui_show_profile_locked(void);
void ui_set_usb_state(bool mounted);
void ui_set_ble_state(ui_ble_state_t state);
void ui_show_typing(bool typing);
#define UI_PAIRING_QR_MAX   32
/** The pairing window: the passkey as digits and as a QR code holding qr_text. */
void ui_show_pairing(bool open, uint32_t passkey, const char *qr_text);
void ui_show_passkey(uint32_t passkey, bool show);

/** The on-device apps in launcher order; false past the last one. Callable from any task. */
bool ui_app_info(size_t index, const char **id, const char **name);

#ifdef __cplusplus
}
#endif

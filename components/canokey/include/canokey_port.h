#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CANOKEY_RP_ID_MAX   64

typedef enum {
    CANOKEY_REQUEST_OTHER,
    CANOKEY_REQUEST_REGISTER,   /* makeCredential */
    CANOKEY_REQUEST_SIGN_IN,    /* getAssertion */
    CANOKEY_REQUEST_SELECT,     /* the host asks which key to use (incl. Windows' probe) */
    CANOKEY_REQUEST_RESET,      /* authenticatorReset: erases all credentials */
    CANOKEY_REQUEST_PGP_SIGN,   /* OpenPGP card: signature key */
    CANOKEY_REQUEST_PGP_DECRYPT,
    CANOKEY_REQUEST_PGP_AUTH,   /* authentication key, e.g. ssh through gpg-agent */
} canokey_request_t;

typedef enum {
    CANOKEY_PROFILE_DEFAULT,
    CANOKEY_PROFILE_PROTECTED,      /* keys also depend on a secret from the phone */
    CANOKEY_PROFILE_COUNT,
} canokey_profile_t;

typedef enum {
    CANOKEY_PROFILE_OPEN,
    CANOKEY_PROFILE_NEW,            /* protected, never set up: the first secret sets it up */
    CANOKEY_PROFILE_WAITING,        /* protected, waiting for its secret */
} canokey_profile_state_t;

#define CANOKEY_SECRET_SIZE     32
#define CANOKEY_PROFILE_ID_SIZE 16

/**
 * Called from a timer task. waiting=true: a command waits for user presence; kind and rp_id
 * (possibly empty) describe it. waiting=false: the wait ended; approved tells whether the user
 * confirmed it (otherwise it timed out or the host cancelled).
 */
typedef void (*canokey_presence_cb_t)(bool waiting, canokey_request_t kind, const char *rp_id, bool approved,
                                      void *ctx);

typedef enum {
    CANOKEY_EVENT_PROFILES,         /* what canokey_profiles_get() returns changed */
    CANOKEY_EVENT_LOCKED_REQUEST,   /* a request was refused: the active profile is locked */
} canokey_event_t;

/** Called from the canokey task; must not call back into the functions below that wait. */
typedef void (*canokey_event_cb_t)(canokey_event_t event, void *ctx);

/** Mounts the security key's storage, installs FIDO and OpenPGP and serves CTAPHID and CCID. */
esp_err_t canokey_start(canokey_presence_cb_t presence_cb, canokey_event_cb_t event_cb, void *ctx);

typedef struct {
    canokey_profile_t active;
    canokey_profile_t default_profile;          /* active after power-up */
    canokey_profile_state_t protected_state;
    bool has_protected_id;
    uint8_t protected_id[CANOKEY_PROFILE_ID_SIZE];  /* made when the protected profile is set up */
} canokey_profiles_t;

/** Callable from any task. */
void canokey_profiles_get(canokey_profiles_t *out);

/*
 * The functions below run on the canokey task. With wait, they block until done and return the
 * result; otherwise they only queue the change. Not callable from the canokey task.
 */

/** Makes a profile active; a locked one refuses every request until it opens. */
esp_err_t canokey_select_profile(canokey_profile_t profile, bool make_default, bool wait);

/**
 * Opens the protected profile with the phone's secret, or sets it up while it is new. Waits.
 * ESP_ERR_INVALID_ARG: the secret doesn't open it. ESP_ERR_INVALID_SIZE: not CANOKEY_SECRET_SIZE.
 */
esp_err_t canokey_set_secret(const uint8_t *secret, size_t len);

/** Erases a profile's FIDO credentials and OpenPGP card. The caller has had the user confirm it. */
esp_err_t canokey_reset_profile(canokey_profile_t profile, bool wait);

/** "ok", or the startup step that failed (for display, since boot logs are often not visible). */
const char *canokey_status(void);

/** Logs the security key's internal state (debugging aid). */
void canokey_log_diagnostics(void);

/** The user confirmed presence while a FIDO or OpenPGP command was waiting for it. */
void canokey_confirm_presence(void);

/** The user approved a security key reset (CANOKEY_REQUEST_RESET) of this profile's credentials. */
void canokey_confirm_reset(canokey_profile_t profile);

#ifdef __cplusplus
}
#endif

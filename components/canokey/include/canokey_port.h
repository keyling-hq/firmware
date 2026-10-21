#pragma once

#include <stdbool.h>
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
} canokey_request_t;

/**
 * Called from a timer task. waiting=true: a command waits for user presence; kind and rp_id
 * (possibly empty) describe it. waiting=false: the wait ended; approved tells whether the user
 * confirmed it (otherwise it timed out or the host cancelled).
 */
typedef void (*canokey_presence_cb_t)(bool waiting, canokey_request_t kind, const char *rp_id, bool approved,
                                      void *ctx);

/** Mounts the FIDO filesystem, installs the applets and starts the task serving CTAPHID. */
esp_err_t canokey_start(canokey_presence_cb_t presence_cb, void *ctx);

/** "ok", or the startup step that failed (for display, since boot logs are often not visible). */
const char *canokey_status(void);

/** Logs the security key's internal state (debugging aid). */
void canokey_log_diagnostics(void);

/** The user confirmed presence while a FIDO command was waiting for it. */
void canokey_confirm_presence(void);

#ifdef __cplusplus
}
#endif

#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Called from the canokey task when a FIDO command starts or stops waiting for user presence. */
typedef void (*canokey_presence_cb_t)(bool waiting, void *ctx);

/** Mounts the FIDO filesystem, installs the applets and starts the task serving CTAPHID. */
esp_err_t canokey_start(canokey_presence_cb_t presence_cb, void *ctx);

/** "ok", or the startup step that failed (for display, since boot logs are often not visible). */
const char *canokey_status(void);

/** Logs the security key's internal state (debugging aid). */
void canokey_log_diagnostics(void);

/** The user confirmed presence (button press) while a FIDO command was waiting for it. */
void canokey_confirm_presence(void);

#ifdef __cplusplus
}
#endif

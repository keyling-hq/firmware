#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "bridge.pb.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PROTOCOL_VERSION        1

/* Called from the protocol task. */
typedef struct {
    /** Writes bytes to the EVT stream; may block. */
    esp_err_t (*send)(const uint8_t *data, size_t len, void *ctx);
    void (*get_config)(bridge_Config *out, void *ctx);
    bridge_Result (*set_config)(const bridge_Config *in, void *ctx);
    void (*get_status)(bridge_Status *out, void *ctx);
    void (*get_device_info)(bridge_DeviceInfo *out, void *ctx);
    /** Firmware update started, progressed or stopped. After a successful one the dongle restarts. */
    void (*ota_state)(bool active, uint8_t percent, void *ctx);
} protocol_handlers_t;

esp_err_t protocol_start(const protocol_handlers_t *handlers, void *ctx);

/* The functions below never block and may be called from any task, including NimBLE callbacks. */

/** Transport session started or ended. A new session resets the streams and grants credits. */
void protocol_session(bool active);

/** Bytes received on the CMD stream. */
void protocol_rx(const uint8_t *data, size_t len);

/** Stops typing and drops all pending input, from the phone (CTRL) or the dongle itself. */
void protocol_abort(void);

/** Something reported in Status changed; the phone is sent a fresh Status. */
void protocol_status_changed(void);

#ifdef __cplusplus
}
#endif

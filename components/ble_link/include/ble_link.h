#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BLE_LINK_PAIRING_WINDOW_S   60

typedef enum {
    BLE_LINK_ADVERTISING,
    BLE_LINK_SECURING,      /* connected, encryption/pairing in progress */
    BLE_LINK_CONNECTED,     /* encrypted, authenticated and bonded */
} ble_link_state_t;

typedef struct {
    ble_link_state_t state;
    bool bonded;
    bool pairing_window;
} ble_link_status_t;

/* All callbacks run in the NimBLE host task (or the caller's task for API-triggered changes). */
typedef struct {
    void (*status)(const ble_link_status_t *status, void *ctx);
    /** show=false hides a previously shown passkey (pairing finished or failed). */
    void (*passkey)(uint32_t passkey, bool show, void *ctx);
    /** Data written to the NUS RX characteristic by the bonded phone. */
    void (*rx)(const uint8_t *data, size_t len, void *ctx);
} ble_link_callbacks_t;

esp_err_t ble_link_start(const char *device_name, const ble_link_callbacks_t *callbacks, void *ctx);

/**
 * While no phone is bonded, pairing is always allowed. Once bonded, a new phone may only pair
 * while the window is open; the new bond replaces the old one.
 */
void ble_link_pairing_window(bool open);

/** Deletes the bond and drops the connection. */
esp_err_t ble_link_forget(void);

/** Drops the current connection, e.g. to cancel a pairing in progress. */
void ble_link_disconnect(void);

/** Notify on the NUS TX characteristic. Fails unless the bonded phone is connected and subscribed. */
esp_err_t ble_link_notify(const void *data, size_t len);

#ifdef __cplusplus
}
#endif

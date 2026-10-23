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
    /** The passkey the next pairing will use; renewed when the window opens and after every attempt. */
    uint32_t passkey;
    /** Identity address, most significant byte first. */
    uint8_t address[6];
} ble_link_status_t;

/* All callbacks run in the NimBLE host task (or the caller's task for API-triggered changes). */
typedef struct {
    void (*status)(const ble_link_status_t *status, void *ctx);
    /** show=false hides a previously shown passkey (pairing finished or failed). */
    void (*passkey)(uint32_t passkey, bool show, void *ctx);
    /** Data written to the NUS RX characteristic by the bonded phone. */
    void (*rx)(const uint8_t *data, size_t len, void *ctx);
    /** Bridge session: the bonded phone is connected and subscribed to EVT. */
    void (*bridge_session)(bool active, void *ctx);
    /** Bytes written to the bridge CMD characteristic. Must not block. */
    void (*bridge_rx)(const uint8_t *data, size_t len, void *ctx);
    /** Any write to the bridge CTRL characteristic. Must not block. */
    void (*bridge_abort)(void *ctx);
} ble_link_callbacks_t;

esp_err_t ble_link_start(const char *device_name, const ble_link_callbacks_t *callbacks, void *ctx);

/**
 * While no phone is bonded, pairing is always allowed. Once bonded, a new phone may only pair
 * while the window is open; the new bond replaces the old one. The window opens by itself at
 * startup while no phone is bonded.
 */
void ble_link_pairing_window(bool open);

/** Bluetooth transmit power in dBm, or false if the controller doesn't report it. */
bool ble_link_tx_power_dbm(int *dbm);

/** Deletes the bond and drops the connection. */
esp_err_t ble_link_forget(void);

/** Drops the current connection, e.g. to cancel a pairing in progress. */
void ble_link_disconnect(void);

/** Notify on the NUS TX characteristic. Fails unless the bonded phone is connected and subscribed. */
esp_err_t ble_link_notify(const void *data, size_t len);

/**
 * Send bytes on the bridge EVT stream, split into MTU-sized notifications. Blocks while the
 * stack is out of buffers, so never call it from a NimBLE callback.
 */
esp_err_t ble_link_bridge_send(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

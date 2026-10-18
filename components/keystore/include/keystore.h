#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KEYSTORE_KEY_SIZE           32
#define KEYSTORE_MAX_LABEL          32
#define KEYSTORE_MAX_CONTEXT        64

typedef enum {
    KEYSTORE_ROOT_DEV,      /* random root key in NVS: development only */
    KEYSTORE_ROOT_EFUSE,    /* HMAC key in an eFuse key block, never readable by software */
} keystore_root_t;

typedef struct {
    keystore_root_t root;
    bool secure_boot;
    bool flash_encryption;
} keystore_status_t;

/** Call after NVS is initialised. Creates the dev root key and the reset salt on first boot. */
esp_err_t keystore_init(void);

void keystore_status(keystore_status_t *status);

/** True only with an eFuse root key, secure boot and flash encryption all in place. */
bool keystore_is_secure(void);

/**
 * Derive a key: HMAC-SHA256(root, label || 0x00 || reset_salt || context).
 * Labels name a key's purpose ("fido", "storage", "vault", ...); never reuse one for two purposes.
 * A factory reset changes the salt, so every derived key changes with it.
 */
esp_err_t keystore_derive(const char *label, const void *context, size_t context_len,
                          uint8_t out[KEYSTORE_KEY_SIZE]);

#ifdef __cplusplus
}
#endif

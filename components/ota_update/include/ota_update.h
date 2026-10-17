#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OTA_UPDATE_SHA256_LEN   32

/* Not thread safe: drive an update from a single task. */

/** Erases the inactive app slot. Any update already running is abandoned. */
esp_err_t ota_update_begin(uint32_t size, const uint8_t *sha256, size_t sha256_len);

/** Data must arrive in order: offset is the number of bytes written so far. */
esp_err_t ota_update_write(uint32_t offset, const uint8_t *data, size_t len);

/** Checks size and SHA-256, validates the image and makes it the next boot target. */
esp_err_t ota_update_end(void);

void ota_update_abort(void);

bool ota_update_active(void);

/** 0-100 while an update is running. */
uint8_t ota_update_progress(void);

/**
 * Call once the firmware has proven it works. A freshly updated image that never gets here is
 * rolled back by the bootloader on the next reset.
 */
void ota_update_confirm_boot(void);

#ifdef __cplusplus
}
#endif

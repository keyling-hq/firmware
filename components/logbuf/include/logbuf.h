#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LOGBUF_SIZE     8192

/** Keeps a copy of everything logged from now on; logs still go to the console as before. */
esp_err_t logbuf_start(void);

/**
 * Copies logged bytes from position `since` (bytes logged since boot), at most len. Positions
 * older than the buffer holds start at the oldest byte kept. Returns the number of bytes copied
 * and sets *start to the position of the first one and *next to the position after the newest.
 */
size_t logbuf_read(uint32_t since, uint8_t *buf, size_t len, uint32_t *start, uint32_t *next);

/** The position after the newest byte logged. */
uint32_t logbuf_end(void);

#ifdef __cplusplus
}
#endif

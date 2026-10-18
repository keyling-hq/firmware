#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DATAFS_BASE_PATH    "/data"

/** Mount the littlefs data partition at DATAFS_BASE_PATH, formatting it if it has no filesystem. */
esp_err_t datafs_mount(void);

/** Erase the data partition. Only call while it is not mounted. */
esp_err_t datafs_format(void);

#ifdef __cplusplus
}
#endif

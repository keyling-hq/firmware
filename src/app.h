#pragma once

#include <stdbool.h>
#include "esp_err.h"

/** Starts every subsystem and wires their events together. NVS must be initialised. */
esp_err_t app_start(void);

/**
 * Factory reset happens across a restart, before anything mounts or opens storage:
 * app_factory_reset_request() marks it and restarts; at boot app_factory_reset_pending()
 * reports (once) that the erase must run.
 */
void app_factory_reset_request(void);
bool app_factory_reset_pending(void);

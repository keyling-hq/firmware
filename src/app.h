#pragma once

#include "esp_err.h"

/** Starts every subsystem and wires their events together. NVS must be initialised. */
esp_err_t app_start(void);

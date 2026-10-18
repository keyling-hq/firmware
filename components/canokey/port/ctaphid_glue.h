#pragma once

#include "esp_err.h"

/** Connects canokey's CTAPHID framing to the FIDO interface of the usb_hid component. */
esp_err_t ctaphid_glue_start(void);

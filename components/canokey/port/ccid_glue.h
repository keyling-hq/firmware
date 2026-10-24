#pragma once

#include "esp_err.h"

/** Connects canokey's CCID implementation to the smartcard interface of the usb_hid component. */
esp_err_t ccid_glue_start(void);

/** Feeds queued USB events to canokey's CCID code; call from the canokey task before device_loop(). */
void ccid_glue_poll(void);

void ccid_glue_log_diagnostics(void);

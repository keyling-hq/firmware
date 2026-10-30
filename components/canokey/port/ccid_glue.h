#pragma once

#include <stdbool.h>
#include "esp_err.h"

/** Connects canokey's CCID implementation to the smartcard interface of the usb_hid component. */
esp_err_t ccid_glue_start(void);

/** Feeds queued USB events to canokey's CCID code; call from the canokey task before device_loop(). */
void ccid_glue_poll(void);

/** True while a response is on its way to the host. */
bool ccid_glue_sending(void);

/** Tells the host the card was inserted or removed. Fails while USB can't take it yet: retry. */
esp_err_t ccid_glue_notify_slot(bool present);

void ccid_glue_log_diagnostics(void);

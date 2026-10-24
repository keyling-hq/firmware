#pragma once

#include "canokey_port.h"

/** What the CTAP command currently being processed asks the user to approve. */
void ctap_request_current(canokey_request_t *kind, const char **rp_id);

/** Called after each CTAP command has been processed, on the canokey task. */
void canokey_port_request_done(void);

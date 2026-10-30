#pragma once

#include <stdbool.h>
#include <stdint.h>

/* The active profile isn't open (phantom, waiting for its secret): its keys can't be used. */
bool canokey_port_locked(void);

/* A request was refused because the profile is locked; the user is told. */
void canokey_port_report_locked(void);

/* From the generated ccid.c: the host has been told the card is out of the slot. */
uint8_t canokey_port_card_absent(void);

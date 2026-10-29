#pragma once

#include <stdbool.h>
#include <stdint.h>

/* The active profile isn't open (protected, waiting for its secret): its keys can't be used. */
bool canokey_port_locked(void);

/* A request was refused because the profile is locked; the user is told. */
void canokey_port_report_locked(void);

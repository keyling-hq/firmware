#pragma once

#include "esp_err.h"

/*
 * canokey's files live in two encrypted littlefs stores, each on its own partition with its own
 * key: OpenPGP's ("pgp-*" files) and FIDO's (everything else). Only the canokey task uses them.
 */
esp_err_t canokey_storage_start(void);

/* Erases the FIDO store and gives it a new key; OpenPGP's is untouched. Leaves it empty and mounted. */
esp_err_t canokey_storage_wipe_fido(void);

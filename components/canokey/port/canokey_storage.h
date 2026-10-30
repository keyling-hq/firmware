#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "canokey_port.h"
#include "esp_err.h"

/*
 * canokey's files live in encrypted littlefs stores, each on its own partition with its own key.
 * Every profile has an OpenPGP store ("pgp-*" files) and a FIDO store (everything else); files
 * go to the active profile's. The phone-locked profile's keys also depend on a secret from the
 * phone, held in RAM only. Only the canokey task uses these functions.
 */
esp_err_t canokey_storage_start(void);

/* Routes canokey's files to a profile's stores. Fails if they aren't open. */
esp_err_t canokey_storage_select(canokey_profile_t profile);

canokey_profile_state_t canokey_storage_state(canokey_profile_t profile);

/*
 * Opens the phone-locked profile with the phone's secret, setting up stores that have none yet.
 * A secret that doesn't open a store is forgotten and the store left untouched (ESP_ERR_INVALID_ARG),
 * as is a different secret once one is set.
 */
esp_err_t canokey_storage_set_secret(const uint8_t *secret, size_t len);

/* The phone-locked profile's id, made when it is first set up; false while it isn't. */
bool canokey_storage_phone_locked_id(uint8_t id[CANOKEY_PROFILE_ID_SIZE]);

/*
 * Erases a profile's FIDO store and gives it a new key; OpenPGP's is untouched. The store is
 * empty and open afterwards, or, for a phone-locked profile without its secret, set up once the
 * secret arrives.
 */
esp_err_t canokey_storage_wipe_fido(canokey_profile_t profile);

/* Erases both of a profile's stores. The phone-locked profile returns to CANOKEY_PROFILE_NEW. */
esp_err_t canokey_storage_wipe_profile(canokey_profile_t profile);

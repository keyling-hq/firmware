#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SETTINGS_LAYOUT_LEN     16
#define SETTINGS_BLE_NAME_LEN   30

typedef struct {
    char layout[SETTINGS_LAYOUT_LEN];
    uint8_t key_press_ms;
    uint8_t key_release_ms;
    uint8_t brightness;         /* percent */
    uint16_t screen_timeout_s;
    char ble_name[SETTINGS_BLE_NAME_LEN];
} settings_t;

typedef struct {
    const char *name;
    uint8_t key_press_ms;
    uint8_t key_release_ms;
} settings_speed_preset_t;

extern const settings_speed_preset_t settings_speed_presets[];
extern const size_t settings_speed_preset_count;

/** Loads settings from NVS, falling back to defaults. NVS must be initialised. */
esp_err_t settings_init(void);

void settings_get(settings_t *out);

/** Updates the in-memory copy and persists it. */
esp_err_t settings_set(const settings_t *in);

#ifdef __cplusplus
}
#endif

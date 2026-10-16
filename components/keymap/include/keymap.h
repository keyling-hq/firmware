#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KEYMAP_MAX_STROKES      2

#define KEYMAP_MOD_LCTRL        0x01
#define KEYMAP_MOD_LSHIFT       0x02
#define KEYMAP_MOD_LALT         0x04
#define KEYMAP_MOD_LGUI         0x08
#define KEYMAP_MOD_RCTRL        0x10
#define KEYMAP_MOD_RSHIFT       0x20
#define KEYMAP_MOD_RALT         0x40
#define KEYMAP_MOD_RGUI         0x80

/* The last stroke of this entry is inverted by Caps Lock. */
#define KEYMAP_FLAG_CAPS        0x01

typedef struct {
    uint8_t modifiers;
    uint8_t keycode;
} keymap_stroke_t;

typedef struct {
    uint16_t codepoint;
    uint8_t flags;
    keymap_stroke_t strokes[KEYMAP_MAX_STROKES];
} keymap_entry_t;

typedef struct {
    const char *id;
    const char *name;
    const keymap_entry_t *entries;  /* sorted by codepoint */
    size_t entry_count;
} keymap_layout_t;

size_t keymap_layout_count(void);

const keymap_layout_t *keymap_layout_get(size_t index);

/** NULL if no layout has this id. */
const keymap_layout_t *keymap_layout_find(const char *id);

/**
 * Strokes that produce codepoint on a host using this layout. Control characters
 * (\n, \t, \b) are layout independent; \r yields no strokes so CRLF types one Enter.
 * Returns the number of strokes written to out, 0 if the character can't be typed.
 */
size_t keymap_lookup(const keymap_layout_t *layout, uint32_t codepoint, bool caps_lock,
                     keymap_stroke_t out[KEYMAP_MAX_STROKES]);

#ifdef __cplusplus
}
#endif

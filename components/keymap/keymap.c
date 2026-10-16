#include <string.h>
#include "keymap.h"
#include "hid_keys.h"

extern const keymap_layout_t keymap_layouts[];
extern const size_t keymap_layouts_count;

size_t keymap_layout_count(void)
{
    return keymap_layouts_count;
}

const keymap_layout_t *keymap_layout_get(size_t index)
{
    return index < keymap_layouts_count ? &keymap_layouts[index] : NULL;
}

const keymap_layout_t *keymap_layout_find(const char *id)
{
    for (size_t i = 0; i < keymap_layouts_count; i++) {
        if (strcmp(keymap_layouts[i].id, id) == 0) {
            return &keymap_layouts[i];
        }
    }
    return NULL;
}

static const keymap_entry_t *find_entry(const keymap_layout_t *layout, uint32_t codepoint)
{
    size_t lo = 0;
    size_t hi = layout->entry_count;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        uint16_t cp = layout->entries[mid].codepoint;
        if (cp == codepoint) {
            return &layout->entries[mid];
        }
        if (cp < codepoint) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return NULL;
}

size_t keymap_lookup(const keymap_layout_t *layout, uint32_t codepoint, bool caps_lock,
                     keymap_stroke_t out[KEYMAP_MAX_STROKES])
{
    switch (codepoint) {
    case '\r':
        return 0;
    case '\n':
        out[0] = (keymap_stroke_t){ 0, HID_KEY_ENTER };
        return 1;
    case '\t':
        out[0] = (keymap_stroke_t){ 0, HID_KEY_TAB };
        return 1;
    case '\b':
        out[0] = (keymap_stroke_t){ 0, HID_KEY_BACKSPACE };
        return 1;
    default:
        break;
    }

    const keymap_entry_t *entry = find_entry(layout, codepoint);
    if (!entry) {
        return 0;
    }

    size_t count = entry->strokes[1].keycode ? 2 : 1;
    memcpy(out, entry->strokes, count * sizeof(keymap_stroke_t));
    if (caps_lock && (entry->flags & KEYMAP_FLAG_CAPS)) {
        out[count - 1].modifiers ^= KEYMAP_MOD_LSHIFT;
    }
    return count;
}

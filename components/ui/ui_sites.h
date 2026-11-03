#pragma once

#include <stddef.h>
#include <stdint.h>
#include "lvgl.h"

/*
 * Known sites for the security key prompt: a recognisable icon next to the site name. The name
 * the browser sent stays the text to trust; an unknown or lookalike domain simply gets no icon.
 */
typedef struct {
    const char *name;
    const char *const *domains;     /* NULL-terminated; subdomains match too */
    uint32_t color;                 /* brand color, 0xRRGGBB */
    const lv_image_dsc_t *icon;     /* square alpha mask to tint, or NULL: draw badge instead */
    const char *badge;              /* 1-3 characters */
} ui_site_t;

/* Generated: ui_site_table.c (tools/site_icons.py). */
extern const ui_site_t ui_sites[];
extern const size_t ui_site_count;

/** The site whose domain is rp_id or a parent of it, the longest match winning; NULL if none. */
const ui_site_t *ui_site_find(const char *rp_id);

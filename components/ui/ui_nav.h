#pragma once

#include "ui_options.h"

/* For ui.c: the framework's side of running an app with a root menu. */

/** Starts at an app's root menu, with an empty navigation stack. */
void ui_nav_open(const ui_menu_def_t *root);

/** Builds the current level again (after an overlay, or when shown state changed). */
void ui_nav_show(void);

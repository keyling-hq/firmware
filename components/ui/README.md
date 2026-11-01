# UI: apps and themes

The on-screen UI is split in three layers:

- **Shared code** (`ui.c`): navigation, the button, overlays (security key prompts, pairing,
  typing, updates), the screen timeout, focus, animations, and the data of every screen.
- **Apps** (`ui_settings.c`, `ui_profiles.c`, ...): what each app shows, built with the app API in
  `ui_priv.h`. Apps contain no drawing code.
- **Themes** (`ui_theme_candy.c`, `ui_theme_device.c`): how screens look and move, through the
  theme API in `ui_theme.h`. The user picks one in Settings → Theme; the choice is saved.

Everything runs in the LVGL task. Try changes in the simulator (`sim/README.md`).

## Input rules

One button, the same everywhere:

| Gesture | Lists and dialogs | Launcher |
|---|---|---|
| Click | next item (lists loop from three items up) | next app |
| Double click | back (or screen off where there is no back) | screen off |
| Hold | activate the focused item | open the app |

Hold items ("Hold to erase") fill while held and act once full; releasing early resets them.
Security key prompts accept only the hold: double click does nothing there.

Text that doesn't fit scrolls in a loop: on the focused item only, from its start each time focus
arrives, after a 0.5 s pause. Titles and other text outside items scroll from when the screen
opens. Text that fits never moves. Dialogs whose other items are hidden show a bobbing arrow.

## Writing an app

`ui_app_example.c` is a complete example (enable `CONFIG_UI_APP_EXAMPLE`, or `-DSIM_EXAMPLES=ON`
in the simulator). To add an app:

1. Copy it, and give the `ui_app_t` its own `id` (stable, reported to the phone), `name`, `icon`
   (an `LV_SYMBOL_*`) and `color` (used by themes that color apps).
2. Add the file to `CMakeLists.txt` and the app to `s_apps` in `ui.c` (launcher order).
3. Build screens with:

| Function | Shows |
|---|---|
| `ui_menu_begin(title, back)` | a list; `back` runs on double click, `ui_app_exit` on the root screen |
| `ui_dialog_begin(title, message, back)` | a message with one or two items (e.g. Cancel and a hold) |
| `ui_menu_add(menu, text, cb, arg)` | an item; `cb` runs as soon as a hold is recognised |
| `ui_menu_add_value(menu, text, value, cb, arg)` | an item with its current value beside it |
| `ui_hold_add(menu, text, ms, fn)` | an item that fills while held; `UI_HOLD_*_MS` for durations |
| `ui_menu_mark(menu, item)` | marks the current choice |
| `ui_menu_end(menu, focus)` | shows it; `focus` NULL returns to where the user last was |
| `ui_text_show(title, text, back)` | a page of text that scrolls up slowly when long |
| `ui_navigate(fn)` | switches to another screen after the current event |

`start`, `stop` and `refresh` in `ui_app_t` are optional. An app that drives the host (like the
jiggler) must pause while `ui_host_busy()`.

## Writing a theme

`ui_theme_example.c` ("Quiet") is a small complete theme with its API explained at the top (enable
`CONFIG_UI_THEME_EXAMPLE`, or `-DSIM_EXAMPLES=ON`). To add a theme, copy it, change `id` (saved in
settings, keep it stable) and `name`, add the file to `CMakeLists.txt` and the theme to `s_themes`
in `ui.c`.

A theme fills in:

| Function | Draws |
|---|---|
| `launcher_build`, `launcher_place`, `status_update` | the app launcher and the status (profile, Bluetooth, USB) |
| `menu_build`, `menu_place` | lists, dialogs, security key prompts and results (`m->kind`) |
| `hold_progress` | a hold item's fill |
| `text_build` | a page of text |

`*_place` functions run for every frame of an animation with a continuous position
(`l->pos`, `m->pos`); `ui_wrap()` and `ui_menu_offset()` turn it into each app's or item's
distance from the middle. Helpers in `ui_theme.h` create labels, boxes, scrolling text and tickers.

Fonts must be enabled in `sdkconfig.defaults` (`CONFIG_LV_FONT_*`). Screens have a 48 KB LVGL heap,
shared by the screen being left and the one being built; the simulator prints its use with every
screenshot.

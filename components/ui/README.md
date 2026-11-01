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
| Click | next item; after the last comes the first | next app |
| Double click | back (or screen off where there is no back) | turn the screen by 180° (saved) |
| Hold | activate the focused item | open the app |

Hold items ("Hold to erase") fill while held and act once full; releasing early resets them.
Security key prompts accept only the hold: double click does nothing there.

Text that doesn't fit scrolls in a loop: on the focused item only, from its start each time focus
arrives, after a 0.5 s pause. Titles and other text outside items scroll from when the screen
opens. Text that fits never moves. Dialogs whose other items are hidden show a bobbing arrow.

## Screen behaviour

- **Screen timeout:** off after the time in settings (15 s by default) without input. The first
  press after that only wakes the screen. After 2 minutes off, it wakes on the launcher (or the
  running app's first screen) instead of where it was left.
- **Overlays** take over the screen while their state lasts, highest priority first: firmware
  update, passkey, security key request, its result, typing, pairing window. All but typing wake
  the screen; typing shows nothing about what is typed.
- **Focus memory:** going back, or returning after an overlay, lands on the item the user left
  from. Opening an app starts at its first item.
- **Orientation:** a double click on the launcher turns the picture by 180° (the display's own
  mirroring, so it costs nothing); saved in settings and applied at start.
- **Themes:** Settings → Theme switches at once and is saved; `ui_reload_theme()` applies a theme
  changed from outside the UI (e.g. the phone).

## Built-in apps

| App | Screens |
|---|---|
| Jiggler | moves the mouse every 2 s while running; hold to stop |
| Profiles | switch Main/Phantom, per profile: use, start with this, erase (hold 3 s) |
| Settings | type test, layout, speed, theme, pair new phone, forget phone, About, factory reset |

About is a scrolling page with the firmware version, profile, phone and USB connection, Bluetooth
name, security key status and the device's security state (keys, secure boot, flash encryption,
signed updates).

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
distance from the middle. Helpers in `ui_theme.h`:

| Helper | Does |
|---|---|
| `ui_box`, `ui_label` | plain objects without LVGL's default styles |
| `ui_label_scroll` | one-line text that scrolls by the rules above; `ui_label_scroll_align`, `ui_label_scroll_fit` |
| `ui_label_set_text` | changes the text of either kind of label (e.g. `m->big_label` during an update) |
| `ui_scroll_activate` | starts or stops scrolling text in a subtree (the shared code does it for items) |
| `ui_ticker` | a text page scrolling up slowly in a loop |
| `ui_more_hint` | the bobbing arrow for dialogs with hidden items |
| `ui_tone_color`, `ui_tone_symbol`, `ui_ble_color`, `ui_usb_color` | colors and symbols by state |

Fonts must be enabled in `sdkconfig.defaults` (`CONFIG_LV_FONT_*`). Screens have a 48 KB LVGL heap,
shared by the screen being left and the one being built; the simulator prints its use with every
screenshot.

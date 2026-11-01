# UI: apps and themes

The on-screen UI is split in three layers:

- **Shared code** (`ui.c`): the button, overlays (security key prompts, pairing, typing,
  updates), the screen timeout, focus, animations, and the data of every screen. The menu
  framework (`ui_nav.c`) builds apps' menus from their option tables and keeps the navigation stack.
- **Apps** (`ui_settings.c`, `ui_profiles.c`, ...): what each app offers, described as option
  tables (`ui_options.h`). Apps contain no drawing or navigation code.
- **Themes** (`ui_theme_candy.c`, `ui_theme_device.c`): how screens look and move, through the
  theme API in `ui_theme.h`. The user picks one in Settings → Theme; the choice is saved.

Everything runs in the LVGL task. Try changes in the simulator (`sim/README.md`).

## Input rules

One button, the same everywhere:

| Gesture | Lists and dialogs | Launcher |
|---|---|---|
| Click | next item; after the last comes the first | next app |
| Double click | back one level; at an app's first menu, leave the app | turn the screen by 180° (saved) |
| Hold | activate the focused item | open the app |

Hold items ("Hold to erase") fill while held and act once full; releasing early resets them.
Security key prompts accept only the hold: double click does nothing there. An app can ask that
only its exit item leaves it (the jiggler): there, double click on its first menu turns the screen
off instead.

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
- **Focus memory:** every menu level remembers its focused item. Going back, a refresh, or
  returning after an overlay lands on it. Opening an app starts at its first item.
- **Orientation:** a double click on the launcher turns the picture by 180° (the display's own
  mirroring, so it costs nothing); saved in settings and applied at start.
- **Themes:** Settings → Theme switches at once and is saved; `ui_reload_theme()` applies a theme
  changed from outside the UI (e.g. the phone).
- **Status symbols** on the launcher. In color themes (Candy, Quiet), Bluetooth shows what the
  status LED shows, in step with it (its color, fading as the LED fades: the blue pulse while not
  paired, orange blinks while pairing, the green flash on connecting), then solid blue while
  connected and dim while the paired phone is away. Monochrome themes (Device) show it full while
  connected and faint otherwise. USB is lit while a computer has set the dongle up; dim or faint
  means power only (a charger, or a computer that is off or asleep), so nothing receives
  keystrokes.

## Built-in apps

| App | Screens |
|---|---|
| Jiggler | moves the mouse every 2 s while running; hold to stop |
| Profiles | switch Main/Phantom, per profile: use, start with this, erase (hold 3 s) |
| Settings | type test, layout, speed, theme, pair new phone, forget phone, About, factory reset |

About is a scrolling page with the firmware version, profile, phone and USB connection, Bluetooth
name, security key status and the device's security state (keys, secure boot, flash encryption,
signed updates).

### Choosing the apps in a build

Each app except Settings (pairing and factory reset are there) has a Kconfig option under
"Keyling UI" → "Apps":

| Option | App | Default |
|---|---|---|
| `CONFIG_UI_APP_JIGGLER` | Jiggler | on |
| `CONFIG_UI_APP_PROFILES` | Profiles (without it, profiles still work from the phone) | on |
| `CONFIG_UI_APP_EXAMPLE` | Template: every kind of menu option, doing nothing | off |

Set them in `sdkconfig.defaults` (e.g. `# CONFIG_UI_APP_JIGGLER is not set`) or with
`pio run -e hid -t menuconfig`. An existing `sdkconfig.hid` keeps its values over the defaults:
change it there too, or delete it to start again from the defaults (a full rebuild). The
simulator builds the same apps as `sdkconfig.hid`.

## Writing an app

`ui_app_example.c` (the Template app) uses every kind of option and does nothing but log, with a
comment on each (enable `CONFIG_UI_APP_EXAMPLE`, or `-DSIM_EXAMPLES=ON` in the simulator). To add
an app:

1. Copy it, and give the `ui_app_t` its own `id` (stable, reported to the phone), `name`, `icon`
   (an `LV_SYMBOL_*`) and `color` (used by themes that color apps).
2. Add a Kconfig option for it (`Kconfig`, under "Apps"), the file to `CMakeLists.txt` under that
   option, and the app to `s_apps` in `ui.c` (launcher order) and `ui_priv.h`. In the simulator,
   add it to `ui_optional` in `sim/CMakeLists.txt`.
3. Describe its menus as option tables and point `menu` at the first one:

```c
static const ui_option_t s_main[] = {
    UI_ACTION("Action", action),            /* runs action(arg) when held */
    UI_CHOICE("Choice", &s_color_choice),   /* shows the current one, opens the list */
    UI_TOGGLE("Extra", extra_get, extra_set),
    UI_SUBMENU("Nested", &s_nested_menu),
    UI_CONFIRM("Confirm", &s_confirm),      /* title, message, Cancel and a hold */
    UI_PAGE("Page", page),                  /* fills a scrolling text page */
    UI_EXIT("Hold to exit"),
};
static UI_MENU(s_main_menu, "Template", s_main);
```

| Option | Does |
|---|---|
| `UI_ACTION(text, fn)` | runs `fn(arg)`; the menu is rebuilt after, so values shown in it update |
| `UI_HOLD(text, ms, fn)` | the same, after a hold of `ms` (`UI_HOLD_*_MS`); fills while held |
| `UI_SUBMENU(text, &menu)` | opens another table; `UI_SUBMENU_ARG` passes an arg, read there with `ui_menu_arg()` |
| `UI_CHOICE(text, &choice)` | `ui_choice_t`: count, name(i), current(), set(i); the list marks the current one and returns after a pick |
| `UI_TOGGLE(text, get, set)` | shows On or Off; holding flips it |
| `UI_CONFIRM(text, &confirm)` | `ui_confirm_t`: title (or `title_fn`), message, confirm text, `hold_ms` (0: no fill), `fn` |
| `UI_PAGE(text, fill)` | `fill(buf, len)` writes the text of a scrolling page |
| `UI_EXIT(text)` | leaves the app after a hold |

Every option also takes, through `UI_OPTION(.field = ...)`: `text_fn` (text computed when shown),
`value_fn` (shown beside it), `marked_fn` (the current choice, e.g. the profile in use), `shown_fn`
(hides it) and `arg` (passed to the callbacks). `ui_profiles.c` uses them: one table serves both
profiles, which open it with their profile as `arg`.

Menus: `UI_MENU(name, title, options)`, `UI_MENU_DYN` with a title function, `UI_DIALOG` with a
message (one or two options shown as a dialog). Set `.exit_by_hold` on an app's first menu so only
its `UI_EXIT` leaves it.

Callbacks can call `ui_back(levels)` (leaving the app past its first menu) or `ui_refresh()` (the
framework refreshes after actions and toggles on its own). When state shown in a menu changes from
outside (e.g. a profile unlocked by the phone), the shared code rebuilds the current level.

`start` and `stop` in `ui_app_t` are optional. An app that drives the host (like the jiggler) must
pause while `ui_host_busy()`.

### Custom screens

For a screen the options can't express, an app sets `show` instead of `menu` and builds it with the
lower-level functions in `ui_priv.h` (the framework uses them too), still themed:

| Function | Shows |
|---|---|
| `ui_menu_begin(title, back)` | a list; `back` runs on double click |
| `ui_dialog_begin(title, message, back)` | a message with one or two items |
| `ui_menu_add(menu, text, cb, arg)` | an item; `cb` (an LVGL event callback) runs as soon as a hold is recognised |
| `ui_menu_add_value(menu, text, value, cb, arg)` | an item with its current value beside it |
| `ui_hold_add(menu, text, ms, fn)` | an item that fills while held |
| `ui_menu_mark(menu, item)` | marks the current choice |
| `ui_menu_end(menu, focus)` | shows it; `focus` NULL for the first item |
| `ui_text_show(title, text, back)` | a page of text that scrolls up slowly when long |
| `ui_navigate(fn)` | switches to another screen after the current event |

Such an app handles going back itself and sets `refresh` to rebuild after outside changes.

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
| `ui_tone_color`, `ui_tone_symbol`, `ui_usb_color` | colors and symbols by state |
| `ui_ble_color` | the Bluetooth symbol in step with the status LED, for themes in color |

Fonts must be enabled in `sdkconfig.defaults` (`CONFIG_LV_FONT_*`). Screens have a 48 KB LVGL heap,
shared by the screen being left and the one being built; the simulator prints its use with every
screenshot.

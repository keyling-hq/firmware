# UI simulator

Runs `components/ui` on a PC with the firmware's LVGL version and LVGL settings (read from
`sdkconfig.hid`): same fonts and 48 KB LVGL heap as the dongle. The UI sources are compiled
from `components/ui` directly, so a change shows up in the simulator and in the firmware alike.
Firmware services (settings, Bluetooth, USB, security key) are stand-ins in `sim_platform.c` and
`sim_main.c`. Colors on the dongle's panel differ somewhat from a PC screen.

Modes:

- **Window:** the 160×80 screen at 4× (`--zoom N` to change), the space bar is the button.
- **Screenshots** (`--out DIR SCRIPT...`): runs a script of button presses and firmware events on
  a simulated clock and saves PNGs. The same script gives the same images on every run and every OS.

Both need a firmware build done once, for LVGL in `managed_components`. The commands below run from
the `firmware` folder.

## Windows

Needs CMake and MSYS2's MinGW-w64 toolchain:

```
C:\msys64\usr\bin\pacman.exe -S mingw-w64-x86_64-gcc mingw-w64-x86_64-ninja
```

In PowerShell, once per window:

```
$env:PATH = "C:\msys64\mingw64\bin;$env:PATH"
```

Configure once. The simulator builds the apps `sdkconfig.hid` selects; add `-DSIM_EXAMPLES=ON`
for the Template app and the example theme (`components/ui/README.md`), or
`-DSDKCONFIG=path/to/sdkconfig` to read another one:

```
cmake -S sim -B sim/build -G Ninja -DCMAKE_BUILD_TYPE=Release
```

Build after every change (about a minute the first time, seconds after that):

```
cmake --build sim/build
```

Window:

```
sim\build\keyling_ui_sim.exe
```

Screenshots:

```
sim\build\keyling_ui_sim.exe --out sim\shots sim\scripts\tour.txt
```

## Linux (and WSL)

Needs a compiler, CMake, Ninja, pkg-config and SDL2 for the window (WSL shows it through WSLg).
Without SDL2 it builds for screenshots only. On Debian or Ubuntu:

```
sudo apt install build-essential cmake ninja-build pkg-config libsdl2-dev
```

Configure once. The build folder is `sim/build-linux`, so it doesn't clash with a Windows build of
the same checkout under WSL:

```
cmake -S sim -B sim/build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release
```

Build after every change:

```
cmake --build sim/build-linux
```

Window:

```
sim/build-linux/keyling_ui_sim
```

Screenshots:

```
sim/build-linux/keyling_ui_sim --out sim/shots sim/scripts/tour.txt
```

## Keys in the window

With the window focused:

| Key | Does |
|---|---|
| Space | the button: tap = click, two quick taps = double click, hold 0.6 s = hold |
| `t` | typing on/off |
| `p` | pairing window (passkey and QR code) |
| `f` | security key request ("Sign in to github.com") |
| `x` | the security key request ends without approval ("Not approved") |
| `u` | USB connected/disconnected |
| `b` | next Bluetooth state |
| `l` | "profile locked" message |
| `1`-`9` | switch theme (as the phone would, through settings) |
| `s` | scrolling text speed: 20, 30, 40 (default) or 60 px/s; the console shows it |
| `Esc` | quit |

The screen turns off after 15 s without input, as on the dongle; the first press only wakes it.
Actions the firmware would carry out (pairing, profiles, settings) are printed to the console and
simulated where the UI shows their result. A double click on the launcher turns the screen by
180° on the dongle: the window can't rotate, so the console says so; screenshots are turned.

## Screenshot scripts

Saves a PNG into the `--out` folder for every `shot`, and prints the LVGL heap in use. The
commands (button gestures, waits, firmware events, `theme ID`, `scroll PX_PER_S`) are listed at the top of
`sim_main.c`. `scripts/tour.txt` is a short example; `scripts/themes.txt` shows every kind of
screen in the current theme, so to compare themes run it once per theme, e.g. after a file that
holds `theme device`.

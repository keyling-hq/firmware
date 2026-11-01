# UI simulator

Runs `components/ui` on a PC with the firmware's LVGL version and LVGL settings (read from
`sdkconfig.hid`): same fonts, theme and 32 KB LVGL heap as the dongle. The UI sources are compiled
from `components/ui` directly, so a change shows up in the simulator and in the firmware alike.
Firmware services (settings, Bluetooth, USB, security key) are stand-ins in `sim_platform.c` and
`sim_main.c`. Colors on the dongle's panel differ somewhat from a PC screen.

Modes:

- **Window:** the 160×80 screen at 4× (`--zoom N` to change), the space bar is the button.
- **Screenshots** (`--out DIR SCRIPT...`): runs a script of button presses and firmware events on
  a simulated clock and saves PNGs. The same script gives the same images on every run and every OS.
- **Design mockups** (`--mockups`): design concepts to try in a window, or rendered as images with
  `--out DIR`; see below.

All need a firmware build done once, for LVGL in `managed_components`. The commands below run from
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

Configure once:

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
| `u` | USB connected/disconnected |
| `b` | next Bluetooth state |
| `l` | "profile locked" message |
| `Esc` | quit |

The screen turns off after 15 s without input, as on the dongle; the first press only wakes it.
Actions the firmware would carry out (pairing, profiles, settings) are printed to the console and
simulated where the UI shows their result.

## Design mockups

`mockups.c` holds design concepts: small prototypes drawn with plain LVGL, separate from
`components/ui`, for comparing looks before building one into the real UI. Each concept draws the
same states: the launcher, an app's menu, and the security key prompt and its result.

Try them in a window:

```
sim\build\keyling_ui_sim.exe --mockups
```

| Key | Does |
|---|---|
| Space | the button: click = next app or item, hold = open the app (hold to approve in the prompt), double click = back |
| `1` `2` `3` | switch concept, keeping the current state |
| `f` | security key prompt; holding fills it, releasing early resets it |
| `Esc` | quit |

Or render each concept as one contact sheet (four states in a 2×2 grid, at 3×) into a folder:

```
sim\build\keyling_ui_sim.exe --mockups --out sim\shots
```

On Linux, the same with `sim/build-linux/keyling_ui_sim`.

Besides the firmware's fonts, mockups can use Montserrat 10, 16 and 20 and the UNSCII 8 and 16
pixel fonts. These are enabled in the simulator only: a design that uses one needs it enabled in
the firmware's sdkconfig too.

## Screenshot scripts

Saves a PNG into the `--out` folder for every `shot`. The commands (button gestures, waits,
firmware events) are listed at the top of `sim_main.c`; `scripts/tour.txt` is an example.

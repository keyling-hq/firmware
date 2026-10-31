# UI simulator

Runs `components/ui` on Windows with the firmware's LVGL version and LVGL settings (read from
`sdkconfig.hid`): same fonts, theme and 32 KB LVGL heap as the dongle. The UI sources are compiled
from `components/ui` directly, so a change shows up in the simulator and in the firmware alike.
Firmware services (settings, Bluetooth, USB, security key) are stand-ins in `sim_platform.c` and
`sim_main.c`. Colors on the dongle's panel differ somewhat from a PC screen.

Needs MSYS2's MinGW-w64 toolchain (`pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-ninja`),
CMake, and a firmware build done once (for LVGL in `managed_components`).

## Build

From the `firmware` folder, in PowerShell. Once per PowerShell window:

```
$env:PATH = "C:\msys64\mingw64\bin;$env:PATH"
```

Once:

```
cmake -S sim -B sim/build -G Ninja -DCMAKE_BUILD_TYPE=Release
```

After every change (the first build takes about a minute, later ones seconds):

```
cmake --build sim/build
```

## Window

```
sim\build\keyling_ui_sim.exe
```

Opens the 160×80 screen at 4× (`--zoom N` to change). With the window focused:

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

## Screenshots

```
sim\build\keyling_ui_sim.exe --out sim\shots sim\scripts\tour.txt
```

Runs the script on a simulated clock and saves a PNG for every `shot`, so the same script gives the
same images every time. The commands (button gestures, waits, firmware events) are listed at the
top of `sim_main.c`; `sim/scripts/tour.txt` is an example.

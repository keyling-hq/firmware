# UI simulator

Runs `components/ui` on Windows with the firmware's LVGL version and LVGL settings (read from
`sdkconfig.hid`): same fonts, theme and 32 KB LVGL heap as the dongle. Firmware services (settings,
Bluetooth, USB, security key) are stand-ins in `sim_platform.c` and `sim_main.c`.

Needs MSYS2's MinGW-w64 toolchain (`pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-ninja`),
CMake, and a firmware build done once (for LVGL in `managed_components`).

## Build

From the `firmware` folder, in PowerShell:

```
$env:PATH = "C:\msys64\mingw64\bin;$env:PATH"
```

```
cmake -S sim -B sim/build -G Ninja -DCMAKE_BUILD_TYPE=Release
```

```
cmake --build sim/build
```

## Window

```
sim\build\keyling_ui_sim.exe
```

Space is the button (click, double click, hold). Keys for events from the firmware: `t` typing,
`p` pairing window, `f` security key request, `u` USB, `b` Bluetooth state, `l` profile locked,
`Esc` quits.

## Screenshots

```
sim\build\keyling_ui_sim.exe --out sim\shots sim\scripts\tour.txt
```

Runs the script on a simulated clock and saves a PNG for every `shot`. The commands are listed at
the top of `sim_main.c`.

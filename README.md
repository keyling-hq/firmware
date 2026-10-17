# Keyling firmware

Firmware for Keyling, a USB dongle that types for your phone: it enumerates as a USB keyboard on
the computer it is plugged into and receives what to type over Bluetooth LE from a paired phone.
Runs on the LilyGo T-Dongle-S3 (ESP32-S3).

- Build: PlatformIO with ESP-IDF (`pio run -e hid`; `-e dev` keeps the USB console and JTAG).
- Protocol: `proto/bridge.proto`; reference client in `tools/client.py`.
- Android-side notes: `docs/android-handoff.md`.

## License

Copyright (C) 2026 the Keyling authors.

This program is free software: you can redistribute it and/or modify it under the terms of the
GNU General Public License as published by the Free Software Foundation, either version 2 of the
License, or (at your option) any later version. See `LICENSE`.

Third-party code keeps its own license:

| Component | License |
|---|---|
| `components/nanopb` (nanopb runtime) | zlib, see `components/nanopb/LICENSE.txt` |
| `components/bsp/st7735` (from LilyGo's T-Dongle-S3 SDK) | MIT, see `components/bsp/st7735/LICENSE` |
| `components/keymap/keymap_layouts.c` (generated from xkeyboard-config data) | MIT/X11 |
| ESP-IDF, NimBLE, esp_tinyusb, esp_lvgl_port, button (fetched at build time) | Apache-2.0 |
| LVGL, TinyUSB (fetched at build time) | MIT |

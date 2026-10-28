# Keyling firmware

Firmware for Keyling, a USB dongle that types for your phone: it enumerates as a USB keyboard on
the computer it is plugged into and receives what to type over Bluetooth LE from a paired phone.
Runs on the LilyGo T-Dongle-S3 (ESP32-S3). It is also a FIDO2/U2F security key, built on
[canokey-core](https://github.com/canokeys/canokey-core).

- Clone with `git clone --recurse-submodules` (or run `git submodule update --init` afterwards).
- Build: PlatformIO with ESP-IDF (`pio run -e hid`; `-e jtag` trades USB for a console and JTAG, for debugging only).
- Protocol: `proto/` is the [keyling-hq/protocol](https://github.com/keyling-hq/protocol)
  submodule; after updating it, regenerate with `tools/gen_proto.py`. Reference client in
  `tools/client.py`.
- Checks (also run by CI): `tools/.venv/Scripts/ruff check .` and `tools/.venv/Scripts/pyright`, with
  `tools/requirements-dev.txt` installed in the venv.
- CI checks every push and pull request; firmware is only built for releases: pushing a tag
  `vX.Y.Z` builds the `hid` firmware and publishes a GitHub release.

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
| `components/canokey/canokey-core` (submodule, includes TinyCBOR) | Apache-2.0 (TinyCBOR: MIT) |
| `components/canokey/port/monocypher` | BSD-2-Clause or CC0-1.0, see `LICENCE.md` there |
| `components/keymap/keymap_layouts.c` (generated from xkeyboard-config data) | MIT/X11 |
| ESP-IDF, NimBLE, esp_tinyusb, esp_lvgl_port, button (fetched at build time) | Apache-2.0 |
| littlefs (fetched at build time) | BSD-3-Clause |
| LVGL, TinyUSB (fetched at build time) | MIT |

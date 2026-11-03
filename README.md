# Keyling firmware

Firmware for Keyling, a USB dongle that types for your phone: it enumerates as a USB keyboard on
the computer it is plugged into and receives what to type over Bluetooth LE from a paired phone.
Runs on the LilyGo T-Dongle-S3 (ESP32-S3).

- **Keyboard bridge:** typing, key combos and media keys from the phone, with keyboard layouts on
  the dongle. Pairing by QR code or passkey.
- **Security key:** FIDO2/U2F (passkeys, `ssh -sk` keys) and an OpenPGP smartcard over CCID
  (GnuPG), built on [canokey-core](https://github.com/canokeys/canokey-core). FIDO requests, and
  OpenPGP ones with touch on, are confirmed by holding the button.
- **Profiles:** Main and Phantom, each with its own encrypted FIDO and OpenPGP storage; Phantom
  only opens with a secret from the phone.
- **On-device apps** on the 160×80 screen with one button: jiggler, profiles, settings. Two themes
  (Candy and Device); a double click on the launcher turns the screen by 180°.
- **Updates over Bluetooth;** release firmware accepts only signed updates.

- Clone with `git clone --recurse-submodules` (or run `git submodule update --init` afterwards).
- Build: PlatformIO with ESP-IDF (`pio run -e hid`; `-e jtag` trades USB for a console and JTAG, for debugging only).
- Protocol: `proto/` is the [keyling-hq/protocol](https://github.com/keyling-hq/protocol)
  submodule; after updating it, regenerate with `tools/gen_proto.py`. Reference client in
  `tools/client.py`.
- Checks (also run by CI): `tools/.venv/Scripts/ruff check .` and `tools/.venv/Scripts/pyright`, with
  `tools/requirements-dev.txt` installed in the venv.
- CI checks and builds (`hid` and `release`) every push and pull request. Pushing a tag `vX.Y.Z`
  builds `release`, signs it after approval (`tools/sign_release.sh`) and publishes a GitHub release.
  Release firmware only accepts Bluetooth updates signed with the same key.
- UI simulator: `sim/` runs the on-screen UI on Windows or Linux, in a window or as scripted screenshots;
  see [sim/README.md](sim/README.md).
- On-screen UI: apps and themes, how to add them, and which apps a build includes (Kconfig):
  [components/ui/README.md](components/ui/README.md).

## License

Copyright (C) 2026 the Keyling authors.

This program is free software: you can redistribute it and/or modify it under the terms of the
GNU General Public License, version 3 only, as published by the Free Software Foundation
(SPDX: `GPL-3.0-only`). See `LICENSE`.

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
| Fonts built into LVGL: Montserrat; UNSCII; Font Awesome Free symbols (`LV_SYMBOL_*`) | OFL-1.1; public domain; CC BY 4.0 (icons) and OFL-1.1 (font) |
| `components/ui/fonts/ui_font_atkinson_mono_14.c` (Atkinson Hyperlegible Mono) | OFL-1.1, see `components/ui/fonts/OFL.txt` |
| `components/ui/ui_site_table.c` (site icons, generated from Simple Icons) | CC0-1.0; the logos are trademarks of their owners, shown only to identify the site |

---
name: code-style
description: Code and comment conventions for this firmware project. Use whenever writing or editing source, CMake, Kconfig, sdkconfig.defaults or platformio.ini files here.
---

# Comments

Comments describe the code they sit next to, for someone reading the code later. They are not a conversation log.

Write a comment only when it adds something the code can't say:
- a non-obvious hardware fact (`/* backlight is active low */`)
- a threading or locking constraint (`/* runs in timer context: never touch LVGL */`)
- why a less obvious approach was chosen, when the reason lives in the code itself

Do not write:
- project history or decisions ("pure ESP-IDF, no Arduino", "copied from the LilyGo SDK")
- plans or future work ("[env:hid] gets added with the usb_hid component", "TODO after TinyUSB")
- restatements of the line below (`/* init NVS */` over `nvs_init();`)
- version trivia that will go stale ("ships ESP-IDF v6.1")

Discussion and rationale belong in the chat or a commit message, never in the file.

# Code

- Match the surrounding code: ESP-IDF style C, 4-space indent, `static const char *TAG`, `ESP_RETURN_ON_ERROR` for error paths.
- Hardware constants live in `components/bsp/include/bsp/tdongle.h`.
- Only the LVGL task touches LVGL; other contexts queue messages or hold `lvgl_port_lock()`.

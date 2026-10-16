#pragma once

/* HID keyboard page usages for keys that don't depend on the host layout. */
#define HID_KEY_ENTER           0x28
#define HID_KEY_ESCAPE          0x29
#define HID_KEY_BACKSPACE       0x2A
#define HID_KEY_TAB             0x2B
#define HID_KEY_SPACE           0x2C
#define HID_KEY_CAPS_LOCK       0x39
#define HID_KEY_F1              0x3A
#define HID_KEY_F12             0x45
#define HID_KEY_PRINT_SCREEN    0x46
#define HID_KEY_SCROLL_LOCK     0x47
#define HID_KEY_PAUSE           0x48
#define HID_KEY_INSERT          0x49
#define HID_KEY_HOME            0x4A
#define HID_KEY_PAGE_UP         0x4B
#define HID_KEY_DELETE          0x4C
#define HID_KEY_END             0x4D
#define HID_KEY_PAGE_DOWN       0x4E
#define HID_KEY_ARROW_RIGHT     0x4F
#define HID_KEY_ARROW_LEFT      0x50
#define HID_KEY_ARROW_DOWN      0x51
#define HID_KEY_ARROW_UP        0x52
#define HID_KEY_MENU            0x65

/* Consumer page usages */
#define HID_USAGE_CONSUMER_PLAY_PAUSE       0x00CD
#define HID_USAGE_CONSUMER_SCAN_NEXT        0x00B5
#define HID_USAGE_CONSUMER_SCAN_PREVIOUS    0x00B6
#define HID_USAGE_CONSUMER_STOP             0x00B7
#define HID_USAGE_CONSUMER_MUTE             0x00E2
#define HID_USAGE_CONSUMER_VOLUME_UP        0x00E9
#define HID_USAGE_CONSUMER_VOLUME_DOWN      0x00EA

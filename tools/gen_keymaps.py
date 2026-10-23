#!/usr/bin/env python3
"""Generate components/keymap/keymap_layouts.c from xkb symbol files.

Usage (any Linux with xkb-data and x11proto-dev, e.g. WSL):
    python3 tools/gen_keymaps.py --xkb /usr/share/X11/xkb \
        --keysymdef /usr/include/X11/keysymdef.h \
        -o components/keymap/keymap_layouts.c
"""

import argparse
import re
import unicodedata
from pathlib import Path

MOD_LSHIFT = 0x02
MOD_RALT = 0x40

LEVEL_MODS = [0, MOD_LSHIFT, MOD_RALT, MOD_RALT | MOD_LSHIFT]

# xkb key name -> HID usage (keyboard page)
XKB_TO_HID = {
    "TLDE": 0x35,
    "AE01": 0x1E, "AE02": 0x1F, "AE03": 0x20, "AE04": 0x21, "AE05": 0x22,
    "AE06": 0x23, "AE07": 0x24, "AE08": 0x25, "AE09": 0x26, "AE10": 0x27,
    "AE11": 0x2D, "AE12": 0x2E,
    "AD01": 0x14, "AD02": 0x1A, "AD03": 0x08, "AD04": 0x15, "AD05": 0x17,
    "AD06": 0x1C, "AD07": 0x18, "AD08": 0x0C, "AD09": 0x12, "AD10": 0x13,
    "AD11": 0x2F, "AD12": 0x30,
    "AC01": 0x04, "AC02": 0x16, "AC03": 0x07, "AC04": 0x09, "AC05": 0x0A,
    "AC06": 0x0B, "AC07": 0x0D, "AC08": 0x0E, "AC09": 0x0F, "AC10": 0x33,
    "AC11": 0x34, "BKSL": 0x31,
    "AB01": 0x1D, "AB02": 0x1B, "AB03": 0x06, "AB04": 0x19, "AB05": 0x05,
    "AB06": 0x11, "AB07": 0x10, "AB08": 0x36, "AB09": 0x37, "AB10": 0x38,
    "LSGT": 0x64,
    "SPCE": 0x2C,
}

# Dead keysym -> (combining mark, spacing character produced by dead key + space)
DEAD_KEYS = {
    "dead_acute": ("́", "´"),
    "dead_grave": ("̀", "`"),
    "dead_circumflex": ("̂", "^"),
    "dead_diaeresis": ("̈", "¨"),
    "dead_tilde": ("̃", "~"),
}

# AltGr levels are only trusted for the "KEY:keysym" pairs listed here: xkb layouts carry
# many AltGr symbols that the matching Windows layouts don't have, or have on other keys.
LAYOUTS = [
    {
        "id": "en-US",
        "name": "English (US)",
        "xkb": "us(basic)",
        "altgr": set(),
    },
    {
        "id": "es-419",
        "name": "Spanish (LatAm)",
        "xkb": "latam(basic)",
        "altgr": {"AD01:at", "AE11:backslash", "AD12:asciitilde", "TLDE:notsign",
                  "AC11:dead_circumflex", "BKSL:dead_grave"},
    },
]

# Keys every layout defines outside its own symbols file.
COMMON_KEYS = {"SPCE": ["space", "space"]}


def load_keysyms(path):
    keysyms = {}
    pattern = re.compile(r"#define XK_(\w+)\s+0x[0-9a-fA-F]+\s*/\*[ (]*U\+([0-9A-Fa-f]{4,6})")
    for line in Path(path).read_text(encoding="latin-1").splitlines():
        m = pattern.match(line)
        if m:
            keysyms.setdefault(m.group(1), chr(int(m.group(2), 16)))
    return keysyms


def keysym_to_char(name, keysyms):
    if len(name) == 1:
        return name
    m = re.fullmatch(r"U([0-9A-Fa-f]{4,6})", name)
    if m:
        return chr(int(m.group(1), 16))
    return keysyms.get(name)


class XkbSymbols:
    def __init__(self, root):
        self.root = Path(root) / "symbols"
        self.cache = {}

    def _sections(self, filename):
        if filename not in self.cache:
            text = (self.root / filename).read_text(encoding="utf-8")
            text = re.sub(r"//[^\n]*", "", text)
            sections, default = {}, None
            for m in re.finditer(r'((?:\w+\s+)*)xkb_symbols\s+"([^"]+)"\s*\{', text):
                body, depth, i = [], 1, m.end()
                while depth:
                    c = text[i]
                    depth += c == "{"
                    depth -= c == "}"
                    body.append(c)
                    i += 1
                sections[m.group(2)] = "".join(body[:-1])
                if "default" in m.group(1).split() and default is None:
                    default = m.group(2)
            self.cache[filename] = (sections, default or next(iter(sections)))
        return self.cache[filename]

    def resolve(self, ref):
        """Return {key name: [keysym per level]} for 'file(section)'."""
        m = re.fullmatch(r"(\w+)(?:\((\w+)\))?", ref)
        assert m, f"bad include reference {ref!r}"
        sections, default = self._sections(m.group(1))
        body = sections[m.group(2) or default]
        keys = {}
        pos = 0
        token = re.compile(r'include\s+"([^"]+)"|key\s*<(\w+)>\s*\{([^}]*)\}')
        for t in token.finditer(body, pos):
            if t.group(1):
                for part in t.group(1).split("+"):
                    if part:
                        keys.update(self.resolve(part))
            else:
                levels = re.search(r"\[([^\]]*)\]", t.group(3))
                if levels:
                    keys[t.group(2)] = [s.strip() for s in levels.group(1).split(",")]
        return keys


def build_layout(spec, xkb, keysyms):
    direct = {}
    dead = {}
    for key, levels in {**COMMON_KEYS, **xkb.resolve(spec["xkb"])}.items():
        hid = XKB_TO_HID.get(key)
        if hid is None:
            continue
        chars = [keysym_to_char(s, keysyms) for s in levels]
        alpha = (len(chars) >= 2 and chars[0] and chars[1] and chars[0].isalpha()
                 and chars[0] != chars[1] and chars[0].upper() == chars[1])
        for level, sym in enumerate(levels[:4]):
            if level >= 2 and f"{key}:{sym}" not in spec["altgr"]:
                continue
            stroke = (LEVEL_MODS[level], hid)
            if sym in DEAD_KEYS:
                dead.setdefault(sym, stroke)
                continue
            ch = chars[level]
            if ch and ch not in direct:
                direct[ch] = ([stroke], alpha and level < 2)

    entries = dict(direct)
    for sym, dead_stroke in dead.items():
        mark, spacing = DEAD_KEYS[sym]
        if " " in direct and spacing not in entries:
            entries[spacing] = ([dead_stroke, direct[" "][0][0]], False)
        for base, (strokes, caps) in direct.items():
            if not ("a" <= base.lower() <= "z") or len(strokes) != 1:
                continue
            composed = unicodedata.normalize("NFC", base + mark)
            if len(composed) == 1 and 0xA0 <= ord(composed) <= 0xFF and composed not in entries:
                entries[composed] = ([dead_stroke, strokes[0]], caps)
    return entries


def emit(layouts, out):
    lines = ["/* Generated by tools/gen_keymaps.py. Do not edit. */", "",
             '#include "keymap.h"', ""]
    for spec, entries in layouts:
        sym = "s_" + spec["id"].replace("-", "_").lower()
        lines.append(f"static const keymap_entry_t {sym}[] = {{")
        for ch in sorted(entries, key=ord):
            strokes, caps = entries[ch]
            s = ", ".join(f"{{0x{m:02X}, 0x{k:02X}}}" for m, k in strokes)
            flags = "KEYMAP_FLAG_CAPS" if caps else "0"
            name = unicodedata.name(ch, f"U+{ord(ch):04X}").lower()
            lines.append(f"    {{0x{ord(ch):04X}, {flags}, {{{s}}}}}, /* {name} */")
        lines.append("};")
        lines.append("")
    lines.append("const keymap_layout_t keymap_layouts[] = {")
    for spec, _ in layouts:
        sym = "s_" + spec["id"].replace("-", "_").lower()
        lines.append(f'    {{"{spec["id"]}", "{spec["name"]}", {sym}, sizeof({sym}) / sizeof({sym}[0])}},')
    lines.append("};")
    lines.append("")
    lines.append("const size_t keymap_layouts_count = sizeof(keymap_layouts) / sizeof(keymap_layouts[0]);")
    Path(out).write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--xkb", default="/usr/share/X11/xkb")
    ap.add_argument("--keysymdef", default="/usr/include/X11/keysymdef.h")
    ap.add_argument("-o", "--output", required=True)
    args = ap.parse_args()

    keysyms = load_keysyms(args.keysymdef)
    xkb = XkbSymbols(args.xkb)
    layouts = [(spec, build_layout(spec, xkb, keysyms)) for spec in LAYOUTS]
    for spec, entries in layouts:
        print(f"{spec['id']}: {len(entries)} characters")
    emit(layouts, args.output)


if __name__ == "__main__":
    main()

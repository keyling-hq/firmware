"""Arranges PNG screenshots of equal size in a grid, for README images.

    python tools/contact_sheet.py OUT.png COLUMNS IN.png...

Reads and writes 8-bit RGB PNGs (the UI simulator's screenshots) with the standard library only.
"""

import struct
import sys
import zlib
from pathlib import Path

GAP = 12
BACKGROUND = b"\x40\x40\x40"


def read_png(path: Path) -> tuple[int, int, list[bytes]]:
    data = path.read_bytes()
    pos, width, height, idat = 8, 0, 0, b""
    while pos < len(data):
        (length,) = struct.unpack(">I", data[pos : pos + 4])
        kind = data[pos + 4 : pos + 8]
        chunk = data[pos + 8 : pos + 8 + length]
        if kind == b"IHDR":
            width, height = struct.unpack(">II", chunk[:8])
            if chunk[8] != 8 or chunk[9] != 2:
                raise SystemExit(f"{path}: only 8-bit RGB PNGs are supported")
        elif kind == b"IDAT":
            idat += chunk
        pos += 12 + length
    raw = zlib.decompress(idat)
    stride = 1 + width * 3
    rows = []
    for y in range(height):
        if raw[y * stride] != 0:
            raise SystemExit(f"{path}: only unfiltered rows are supported")
        rows.append(raw[y * stride + 1 : (y + 1) * stride])
    return width, height, rows


def write_png(path: Path, width: int, height: int, rows: list[bytes]) -> None:
    def chunk(kind: bytes, body: bytes) -> bytes:
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))

    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    pixels = zlib.compress(b"".join(b"\0" + row for row in rows), 9)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", pixels) + chunk(b"IEND", b""))


def main() -> None:
    if len(sys.argv) < 4:
        raise SystemExit(__doc__)
    out, columns = Path(sys.argv[1]), int(sys.argv[2])
    images = [read_png(Path(name)) for name in sys.argv[3:]]
    width, height = images[0][0], images[0][1]
    if any((w, h) != (width, height) for w, h, _ in images):
        raise SystemExit("all screenshots must have the same size")
    grid_rows = (len(images) + columns - 1) // columns
    sheet_w = columns * width + (columns + 1) * GAP
    sheet_h = grid_rows * height + (grid_rows + 1) * GAP
    sheet = [bytearray(BACKGROUND * sheet_w) for _ in range(sheet_h)]
    for index, (_, _, rows) in enumerate(images):
        x = GAP + (index % columns) * (width + GAP)
        y = GAP + (index // columns) * (height + GAP)
        for dy, row in enumerate(rows):
            sheet[y + dy][x * 3 : (x + width) * 3] = row
    write_png(out, sheet_w, sheet_h, [bytes(row) for row in sheet])


if __name__ == "__main__":
    main()

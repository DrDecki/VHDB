import os
import struct
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(os.path.dirname(HERE), "vita", "sce_sys")

PALETTE = [
    (0x16, 0x16, 0x14),
    (0x1A, 0x1A, 0x18),
    (0x1F, 0x1F, 0x1C),
    (0x24, 0x24, 0x22),
    (0x2C, 0x2B, 0x28),
    (0x3A, 0x39, 0x35),
    (0x8A, 0x88, 0x80),
    (0xE8, 0xE6, 0xE0),
    (0x6A, 0x43, 0x22),
    (0xC0, 0x7A, 0x3E),
]

BACKDROP = 1
PANEL = 3
EDGE = 5
MUTED = 6
BRIGHT = 7
ACCENT_DIM = 8
ACCENT = 9

GLYPHS = {
    " ": ["00000", "00000", "00000", "00000", "00000", "00000", "00000"],
    "-": ["00000", "00000", "00000", "01110", "00000", "00000", "00000"],
    ".": ["00000", "00000", "00000", "00000", "00000", "01100", "01100"],
    "0": ["01110", "10001", "10011", "10101", "11001", "10001", "01110"],
    "1": ["00100", "01100", "00100", "00100", "00100", "00100", "01110"],
    "2": ["01110", "10001", "00001", "00110", "01000", "10000", "11111"],
    "3": ["11110", "00001", "00001", "01110", "00001", "00001", "11110"],
    "4": ["00010", "00110", "01010", "10010", "11111", "00010", "00010"],
    "5": ["11111", "10000", "11110", "00001", "00001", "10001", "01110"],
    "6": ["00110", "01000", "10000", "11110", "10001", "10001", "01110"],
    "7": ["11111", "00001", "00010", "00100", "01000", "01000", "01000"],
    "8": ["01110", "10001", "10001", "01110", "10001", "10001", "01110"],
    "9": ["01110", "10001", "10001", "01111", "00001", "00010", "01100"],
    "A": ["01110", "10001", "10001", "11111", "10001", "10001", "10001"],
    "B": ["11110", "10001", "10001", "11110", "10001", "10001", "11110"],
    "C": ["01110", "10001", "10000", "10000", "10000", "10001", "01110"],
    "D": ["11110", "10001", "10001", "10001", "10001", "10001", "11110"],
    "E": ["11111", "10000", "10000", "11110", "10000", "10000", "11111"],
    "F": ["11111", "10000", "10000", "11110", "10000", "10000", "10000"],
    "G": ["01110", "10001", "10000", "10111", "10001", "10001", "01111"],
    "H": ["10001", "10001", "10001", "11111", "10001", "10001", "10001"],
    "I": ["01110", "00100", "00100", "00100", "00100", "00100", "01110"],
    "J": ["00111", "00010", "00010", "00010", "00010", "10010", "01100"],
    "K": ["10001", "10010", "10100", "11000", "10100", "10010", "10001"],
    "L": ["10000", "10000", "10000", "10000", "10000", "10000", "11111"],
    "M": ["10001", "11011", "10101", "10101", "10001", "10001", "10001"],
    "N": ["10001", "11001", "10101", "10011", "10001", "10001", "10001"],
    "O": ["01110", "10001", "10001", "10001", "10001", "10001", "01110"],
    "P": ["11110", "10001", "10001", "11110", "10000", "10000", "10000"],
    "Q": ["01110", "10001", "10001", "10001", "10101", "10010", "01101"],
    "R": ["11110", "10001", "10001", "11110", "10100", "10010", "10001"],
    "S": ["01111", "10000", "10000", "01110", "00001", "00001", "11110"],
    "T": ["11111", "00100", "00100", "00100", "00100", "00100", "00100"],
    "U": ["10001", "10001", "10001", "10001", "10001", "10001", "01110"],
    "V": ["10001", "10001", "10001", "10001", "01010", "01010", "00100"],
    "W": ["10001", "10001", "10001", "10101", "10101", "11011", "10001"],
    "X": ["10001", "10001", "01010", "00100", "01010", "10001", "10001"],
    "Y": ["10001", "10001", "01010", "00100", "00100", "00100", "00100"],
    "Z": ["11111", "00001", "00010", "00100", "01000", "10000", "11111"],
}


def chunk(tag, data):
    return (struct.pack(">I", len(data)) + tag + data +
            struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))


def write_png(path, width, height, pixels):
    palette = b"".join(bytes(color) for color in PALETTE)
    raw = bytearray()
    for row in pixels:
        raw.append(0)
        raw.extend(row)

    body = b"\x89PNG\r\n\x1a\n"
    body += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 3, 0, 0, 0))
    body += chunk(b"PLTE", palette)
    body += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    body += chunk(b"IEND", b"")

    with open(path, "wb") as handle:
        handle.write(body)


def blank(width, height, color):
    return [bytearray([color] * width) for _ in range(height)]


def draw_text(pixels, value, x, y, scale, color):
    cursor = x
    for letter in value:
        glyph = GLYPHS.get(letter)
        if glyph:
            for row, line in enumerate(glyph):
                for column, cell in enumerate(line):
                    if cell != "1":
                        continue
                    for dy in range(scale):
                        for dx in range(scale):
                            py, px = y + row * scale + dy, cursor + column * scale + dx
                            if 0 <= py < len(pixels) and 0 <= px < len(pixels[0]):
                                pixels[py][px] = color
        cursor += 6 * scale


def fill(pixels, x, y, width, height, color):
    for py in range(y, min(y + height, len(pixels))):
        for px in range(x, min(x + width, len(pixels[0]))):
            if px >= 0 and py >= 0:
                pixels[py][px] = color


def icon(size):
    pixels = blank(size, size, BACKDROP)
    scale = max(1, size // 34)
    width = 4 * 6 * scale

    for y in range(size):
        for x in range(size):
            if x < 6 or y < 6 or x >= size - 6 or y >= size - 6:
                pixels[y][x] = PANEL

    draw_text(pixels, "VHDB", (size - width) // 2, size // 2 - 4 * scale, scale,
              BRIGHT)
    fill(pixels, (size - width) // 2, size // 2 + 5 * scale, width, scale, ACCENT)
    return pixels


def startup(width, height):
    pixels = blank(width, height, PANEL)
    scale = 4
    mark = 4 * 6 * scale

    draw_text(pixels, "VHDB", (width - mark) // 2, height // 2 - 24, scale, BRIGHT)
    fill(pixels, (width - mark) // 2, height // 2 + 14, mark, 3, ACCENT)
    return pixels


def background(width, height):
    pixels = blank(width, height, BACKDROP)

    edge = int(height * 0.58)
    fill(pixels, 0, 0, width, edge, PANEL)
    fill(pixels, 0, edge, width, 2, ACCENT_DIM)

    for y in range(height):
        for x in range(width):
            if pixels[y][x] == PANEL and (x * 3 + y * 5) % 240 < 2:
                pixels[y][x] = EDGE

    mark = 6
    small = 2

    draw_text(pixels, "VHDB", 56, height - 150, mark, BRIGHT)
    fill(pixels, 56, height - 150 + 7 * mark + 14, 4 * 6 * mark - 6, 4, ACCENT)
    draw_text(pixels, "HOMEBREW STORE FOR THE PS VITA", 58,
              height - 150 + 7 * mark + 34, small, MUTED)

    draw_text(pixels, "RUNS ON THE VITAHOMEBREWDB CATALOG", 58,
              height - 150 + 7 * mark + 58, small, EDGE)

    author = "MADE BY DRDECKI"
    draw_text(pixels, author, width - len(author) * 6 * small - 56, height - 52,
              small, MUTED)
    return pixels


def template():
    return """<?xml version="1.0" encoding="utf-8"?>
<livearea style="a1" format-ver="01.00" content-rev="1">
  <livearea-background>
    <image>bg.png</image>
  </livearea-background>

  <gate>
    <startup-image>startup.png</startup-image>
  </gate>
</livearea>
"""


def main():
    os.makedirs(os.path.join(OUT, "livearea", "contents"), exist_ok=True)
    write_png(os.path.join(OUT, "icon0.png"), 128, 128, icon(128))
    write_png(os.path.join(OUT, "livearea", "contents", "startup.png"), 280, 158,
              startup(280, 158))
    write_png(os.path.join(OUT, "livearea", "contents", "bg.png"), 840, 500,
              background(840, 500))

    with open(os.path.join(OUT, "livearea", "contents", "template.xml"), "w") as f:
        f.write(template())

    print("wrote icon0.png, startup.png, bg.png and template.xml")


if __name__ == "__main__":
    main()

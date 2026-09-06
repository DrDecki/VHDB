import os
import struct
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(os.path.dirname(HERE), "vita", "sce_sys")

BACKGROUND = (0x1A, 0x1A, 0x18)
PANEL = (0x24, 0x24, 0x22)
TEXT = (0xE8, 0xE6, 0xE0)
MUTED = (0x8A, 0x88, 0x80)
ACCENT = (0xC0, 0x7A, 0x3E)

PALETTE = [BACKGROUND, PANEL, TEXT, MUTED, ACCENT]

GLYPHS = {
    "V": ["10001", "10001", "10001", "01010", "01010", "00100", "00100"],
    "H": ["10001", "10001", "10001", "11111", "10001", "10001", "10001"],
    "D": ["11110", "10001", "10001", "10001", "10001", "10001", "11110"],
    "B": ["11110", "10001", "10001", "11110", "10001", "10001", "11110"],
}


def chunk(tag, data):
    return (struct.pack(">I", len(data)) + tag + data +
            struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))


def write_png(path, width, height, pixels):
    palette = b"".join(bytes(color) for color in PALETTE)
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        raw.extend(pixels[y])

    body = b"\x89PNG\r\n\x1a\n"
    body += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 3, 0, 0, 0))
    body += chunk(b"PLTE", palette)
    body += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    body += chunk(b"IEND", b"")

    with open(path, "wb") as handle:
        handle.write(body)


def blank(width, height, color=0):
    return [bytearray([color] * width) for _ in range(height)]


def draw_text(pixels, text, x, y, scale, color):
    cursor = x
    for letter in text:
        glyph = GLYPHS.get(letter)
        if glyph:
            for row, line in enumerate(glyph):
                for column, cell in enumerate(line):
                    if cell != "1":
                        continue
                    for dy in range(scale):
                        for dx in range(scale):
                            py = y + row * scale + dy
                            px = cursor + column * scale + dx
                            if 0 <= py < len(pixels) and 0 <= px < len(pixels[0]):
                                pixels[py][px] = color
        cursor += 6 * scale


def rule(pixels, x, y, width, color):
    for px in range(x, min(x + width, len(pixels[0]))):
        if 0 <= y < len(pixels):
            pixels[y][px] = color


def icon(size):
    pixels = blank(size, size, 0)
    for y in range(size):
        for x in range(size):
            if x < 6 or y < 6 or x >= size - 6 or y >= size - 6:
                pixels[y][x] = 1
    scale = max(1, size // 34)
    width = 4 * 6 * scale
    draw_text(pixels, "VHDB", (size - width) // 2, size // 2 - 4 * scale, scale, 2)
    rule(pixels, (size - width) // 2, size // 2 + 5 * scale, width, 4)
    return pixels


def startup(width, height):
    pixels = blank(width, height, 0)
    scale = 4
    text_width = 4 * 6 * scale
    draw_text(pixels, "VHDB", (width - text_width) // 2, height // 2 - 20, scale, 2)
    rule(pixels, (width - text_width) // 2, height // 2 + 16, text_width, 4)
    return pixels


def background(width, height):
    pixels = blank(width, height, 0)
    for y in range(height):
        for x in range(width):
            if ((x * 7 + y * 13) % 211) == 0:
                pixels[y][x] = 1
    rule(pixels, 0, height - 3, width, 4)
    return pixels


def main():
    os.makedirs(os.path.join(OUT, "livearea", "contents"), exist_ok=True)
    write_png(os.path.join(OUT, "icon0.png"), 128, 128, icon(128))
    write_png(os.path.join(OUT, "livearea", "contents", "startup.png"), 280, 158,
              startup(280, 158))
    write_png(os.path.join(OUT, "livearea", "contents", "bg.png"), 840, 500,
              background(840, 500))

    template = """<?xml version="1.0" encoding="utf-8"?>
<livearea style="a1" format-ver="01.00" content-rev="1">
  <livearea-background>
    <image>bg.png</image>
  </livearea-background>
  <gate>
    <startup-image>startup.png</startup-image>
  </gate>
</livearea>
"""
    with open(os.path.join(OUT, "livearea", "contents", "template.xml"), "w") as handle:
        handle.write(template)

    print("wrote icon0.png, startup.png, bg.png and template.xml")


if __name__ == "__main__":
    main()

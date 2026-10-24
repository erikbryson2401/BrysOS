#!/usr/bin/env python3
"""Genera fuentes de mapa de bits (Latin-1, 32..255) a partir de fuentes
TrueType libres (DejaVu) para el kernel de BrysOS. Salida: C++ por stdout."""
from PIL import Image, ImageDraw, ImageFont

FONTS = [
    # nombre, archivo, tamaño en píxeles
    ("font_ui", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 11),
    ("font_ui_bold", "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 11),
    ("font_mono", "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", 12),
    ("font_large", "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 20),
]

FIRST, LAST = 32, 255


def gen(name, path, size):
    font = ImageFont.truetype(path, size)
    ascent, descent = font.getmetrics()
    height = ascent + descent
    widths, rows_all = [], []
    maxw = 0
    for cp in range(FIRST, LAST + 1):
        ch = chr(cp)
        if 0x7F <= cp < 0xA0:
            ch = "?"
        adv = int(round(font.getlength(ch)))
        img = Image.new("1", (max(adv, 1) + 8, height + 4), 0)
        d = ImageDraw.Draw(img)
        d.fontmode = "1"
        d.text((0, 0), ch, font=font, fill=1)
        w = min(max(adv, 1), 32)
        maxw = max(maxw, w)
        rows = []
        for y in range(height):
            bits = 0
            for x in range(w):
                if img.getpixel((x, y)):
                    bits |= 1 << (31 - x)
            rows.append(bits)
        widths.append(w)
        rows_all.append(rows)
    print(f"// Generado por tools/genfont.py a partir de {path.split('/')[-1]} ({size}px)")
    print(f"static const uint32_t {name}_bits[{LAST - FIRST + 1}][{height}] = {{")
    for rows in rows_all:
        print("    {" + ",".join(f"0x{r:08x}" for r in rows) + "},")
    print("};")
    print(f"static const uint8_t {name}_widths[{LAST - FIRST + 1}] = {{" + ",".join(map(str, widths)) + "};")
    print(f"const Font {name} = {{ {height}, {ascent}, {maxw}, &{name}_bits[0][0], {name}_widths }};")
    print()


print('#include "gfx/font.h"\n')
for f in FONTS:
    gen(*f)

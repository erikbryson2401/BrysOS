#!/usr/bin/env python3
"""Convierte BrysOS_logo.png en un bloque binario para el núcleo:
uint32 ancho, uint32 alto y después ancho*alto píxeles 0xAARRGGBB."""
import struct
import sys
from PIL import Image

src, dst, size = sys.argv[1], sys.argv[2], int(sys.argv[3])
im = Image.open(src).convert("RGBA")
im.thumbnail((size, size), Image.LANCZOS)
w, h = im.size
with open(dst, "wb") as f:
    f.write(struct.pack("<II", w, h))
    px = im.load()
    for y in range(h):
        row = bytearray()
        for x in range(w):
            r, g, b, a = px[x, y]
            row += struct.pack("<I", (a << 24) | (r << 16) | (g << 8) | b)
        f.write(row)
print(f"logo {w}x{h}")

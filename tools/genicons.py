#!/usr/bin/env python3
"""Dibuja los iconos de BrysOS (32x32 y 16x16) con primitivas y los exporta
como C++. Con --preview guarda build/icons_preview.png para revisarlos."""
import sys
from PIL import Image, ImageDraw

K, W, G, D, L = (0, 0, 0), (255, 255, 255), (192, 192, 192), (128, 128, 128), (223, 223, 223)
Y, DY = (255, 255, 0), (128, 128, 0)
B, NAVY = (0, 0, 255), (0, 0, 128)
C, TEAL = (0, 255, 255), (0, 128, 128)
R, MAR = (255, 0, 0), (128, 0, 0)
GR, DGR = (0, 255, 0), (0, 128, 0)
FOLD, FOLDD = (255, 223, 107), (196, 150, 40)
BEIGE = (224, 220, 200)


class P:
    """Lienzo con coordenadas de diseño 0..32 escaladas al tamaño real."""
    def __init__(self, size):
        self.n = size
        self.img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
        self.d = ImageDraw.Draw(self.img)
        self.k = size / 32

    def s(self, v):
        return int(round(v * self.k))

    def rect(self, x0, y0, x1, y1, fill=None, outline=None):
        a, b, c, d = self.s(x0), self.s(y0), self.s(x1) - 1, self.s(y1) - 1
        if c < a: c = a
        if d < b: d = b
        self.d.rectangle([a, b, c, d], fill=fill, outline=outline)

    def poly(self, pts, fill=None, outline=None):
        self.d.polygon([(self.s(x), self.s(y)) for x, y in pts], fill=fill, outline=outline)

    def line(self, x0, y0, x1, y1, color, w=1):
        self.d.line([self.s(x0), self.s(y0), self.s(x1), self.s(y1)], fill=color, width=w)

    def ellipse(self, x0, y0, x1, y1, fill=None, outline=None):
        self.d.ellipse([self.s(x0), self.s(y0), self.s(x1) - 1, self.s(y1) - 1], fill=fill, outline=outline)

    def px(self, x, y, color):
        self.d.point((self.s(x), self.s(y)), fill=color)


def computer(p):
    p.rect(5, 2, 28, 21, G, K)
    p.rect(6, 3, 27, 4, W)
    p.rect(8, 5, 25, 17, NAVY, K)
    p.rect(9, 6, 24, 16, TEAL)
    p.rect(10, 7, 14, 9, C)
    p.rect(22, 18, 25, 19, DGR)
    p.rect(13, 21, 20, 23, G, K)
    p.rect(4, 23, 29, 29, G, K)
    p.rect(5, 24, 28, 25, W)
    p.rect(17, 26, 26, 27, D)
    p.rect(6, 26, 9, 27, DGR)


def folder_shape(p, open_=False):
    p.poly([(2, 8), (3, 6), (11, 6), (13, 8), (28, 8), (28, 27), (2, 27)], FOLD, K)
    p.rect(3, 9, 27, 10, (255, 255, 200))
    if open_:
        p.poly([(2, 27), (7, 13), (31, 13), (27, 27)], (255, 240, 150), K)
    p.line(3, 27, 28, 27, FOLDD)


def folder(p):
    folder_shape(p)


def folder_open(p):
    folder_shape(p, True)


def mydocs(p):
    folder_shape(p)
    p.poly([(9, 3), (21, 3), (24, 6), (24, 20), (9, 20)], W, K)
    for y in (8, 11, 14, 17):
        p.line(11, y, 22, y, NAVY)
    p.poly([(2, 14), (28, 14), (28, 27), (2, 27)], FOLD, K)


def recycle(p):
    p.poly([(7, 7), (25, 7), (23, 29), (9, 29)], G, K)
    p.rect(5, 5, 27, 8, L, K)
    for x in (11, 14, 17, 20):
        p.line(x, 9, x + 0.5, 27, D)
    p.line(8, 14, 24, 14, D)
    p.line(8, 21, 24, 21, D)
    p.rect(13, 3, 19, 5, G, K)


def hdd(p):
    p.poly([(3, 12), (9, 7), (30, 7), (27, 12)], L, K)
    p.rect(3, 12, 28, 23, G, K)
    p.poly([(27, 12), (30, 7), (30, 18), (27, 23)], D, K)
    p.rect(5, 18, 9, 20, GR, K)
    p.line(12, 19, 25, 19, D)


def page(p, lines=True, color_lines=K):
    p.poly([(6, 2), (20, 2), (26, 8), (26, 30), (6, 30)], W, K)
    p.poly([(20, 2), (20, 8), (26, 8)], G, K)
    if lines:
        for y in range(12, 28, 3):
            p.line(9, y, 23, y, color_lines)


def file_text(p):
    page(p, True, D)


def file_generic(p):
    page(p, False)
    p.rect(10, 14, 22, 24, G, D)
    p.rect(11, 15, 21, 17, NAVY)


def file_image(p):
    page(p, False)
    p.rect(9, 11, 24, 26, C, K)
    p.poly([(9, 26), (14, 17), (18, 22), (20, 19), (24, 26)], DGR)
    p.ellipse(18, 12, 22, 16, Y)


def notepad(p):
    p.rect(6, 4, 26, 30, W, K)
    p.rect(6, 4, 26, 8, (90, 150, 220), K)
    for x in (9, 13, 17, 21):
        p.rect(x, 2, x + 2, 6, G, K)
    for y in range(12, 28, 3):
        p.line(8, y, 24, y, (90, 150, 220))


def paint(p):
    p.ellipse(2, 9, 26, 29, BEIGE, K)
    p.ellipse(15, 19, 20, 24, (0, 0, 0, 0), K)
    for (x, y, c) in ((6, 13, R), (11, 11, Y), (17, 11, B), (5, 19, DGR), (8, 24, (255, 0, 255))):
        p.ellipse(x, y, x + 4, y + 4, c, K)
    p.line(29, 2, 18, 17, MAR, 2)
    p.poly([(17, 15), (20, 18), (16, 21), (14, 19)], (40, 40, 40))


def calc(p):
    p.rect(6, 2, 26, 30, G, K)
    p.rect(7, 3, 25, 4, W)
    p.rect(8, 5, 24, 10, (200, 230, 200), K)
    p.rect(17, 6, 23, 9, K)
    for i in range(4):
        for j in range(4):
            x = 8 + j * 4.2
            y = 12 + i * 4.5
            p.rect(x, y, x + 3.2, y + 3.5, (R if (j == 3) else W), K)


def cmd(p):
    p.rect(2, 4, 30, 28, G, K)
    p.rect(3, 5, 29, 9, NAVY)
    p.rect(4, 10, 28, 27, K)
    p.line(6, 13, 9, 15, W)
    p.line(9, 15, 6, 17, W)
    p.rect(11, 17, 16, 18, W)


def mine(p):
    p.line(16, 3, 16, 29, K, 2)
    p.line(3, 16, 29, 16, K, 2)
    p.line(7, 7, 25, 25, K, 2)
    p.line(25, 7, 7, 25, K, 2)
    p.ellipse(7, 7, 25, 25, K)
    p.rect(11, 11, 15, 15, W)


def find(p):
    page(p, True, D)
    p.ellipse(9, 9, 23, 23, C, K)
    p.ellipse(10, 10, 22, 22, outline=NAVY)
    p.line(21, 21, 29, 29, MAR, 3)


def programs(p):
    folder_shape(p)
    p.rect(9, 12, 24, 24, W, K)
    p.rect(9, 12, 24, 15, NAVY)


def shutdown(p):
    p.ellipse(4, 4, 28, 28, (180, 0, 0), K)
    p.ellipse(9, 9, 23, 23, outline=W)
    p.ellipse(10, 10, 22, 22, outline=W)
    p.rect(14, 6, 18, 8, (180, 0, 0))
    p.rect(15, 6, 17, 16, W)


def run(p):
    p.rect(3, 6, 29, 27, G, K)
    p.rect(4, 7, 28, 10, NAVY)
    p.rect(6, 13, 26, 24, W, D)
    p.poly([(9, 22), (18, 15), (18, 19), (23, 19), (23, 15)], DGR)
    p.line(10, 21, 20, 16, DGR, 2)


def help_(p):
    p.rect(5, 3, 26, 29, (0, 100, 180), K)
    p.rect(8, 3, 9, 29, NAVY)
    p.ellipse(11, 7, 23, 23, Y, K)
    p.line(15, 12, 17, 10, K, 2)
    p.line(17, 10, 20, 12, K, 2)
    p.line(20, 12, 17, 16, K, 2)
    p.rect(16, 17, 18, 18, K)
    p.rect(16, 19, 18, 21, K)


def settings(p):
    folder_shape(p)
    p.ellipse(10, 11, 22, 23, G, K)
    p.ellipse(14, 15, 18, 19, W, K)
    for (x, y) in ((15, 9), (15, 22), (8, 15), (21, 15)):
        p.rect(x, y, x + 2.5, y + 2.5, G, K)


def app(p):
    p.rect(2, 5, 30, 27, G, K)
    p.rect(3, 6, 29, 10, NAVY)
    p.rect(4, 11, 28, 26, W)


def logo(p):
    # Logotipo de BrysOS (BrysOS_logo.png) reducido; se recorta el margen transparente
    import os
    from PIL import ImageEnhance
    src = Image.open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "BrysOS_logo.png")).convert("RGBA")
    l, t, r, b = src.getbbox()
    # a este tamaño las letras no se leen: sólo el emblema (planeta y anillo, parte superior)
    src = src.crop((l, t, r, t + (b - t) * 66 // 100))
    src = src.crop(src.getbbox())
    src.thumbnail((p.n, p.n), Image.LANCZOS)
    # más contraste para que se distinga sobre el gris de los botones
    rgb = ImageEnhance.Contrast(src.convert("RGB")).enhance(1.4)
    rgb.putalpha(src.getchannel("A"))
    p.img.paste(rgb, ((p.n - rgb.width) // 2, (p.n - rgb.height) // 2), rgb)


def info(p):
    p.ellipse(3, 3, 29, 29, W, K)
    p.rect(14, 7, 18, 11, B)
    p.rect(14, 13, 18, 25, B)


def warning(p):
    p.poly([(16, 2), (30, 28), (2, 28)], Y, K)
    p.rect(15, 10, 18, 20, K)
    p.rect(15, 22, 18, 25, K)


def error(p):
    p.ellipse(3, 3, 29, 29, R, MAR)
    p.line(10, 10, 22, 22, W, 3)
    p.line(22, 10, 10, 22, W, 3)


def question(p):
    p.ellipse(3, 3, 29, 29, W, K)
    p.line(12, 11, 15, 8, B, 3)
    p.line(15, 8, 20, 10, B, 3)
    p.line(20, 10, 16, 17, B, 3)
    p.rect(15, 18, 18, 20, B)
    p.rect(15, 22, 18, 25, B)


def floppy(p):
    p.rect(4, 4, 28, 28, (40, 40, 40), K)
    p.rect(9, 4, 23, 12, G, K)
    p.rect(18, 5, 21, 11, (40, 40, 40))
    p.rect(8, 17, 24, 28, W, K)


def up_folder(p):
    folder_shape(p)
    p.poly([(16, 11), (22, 17), (19, 17), (19, 23), (13, 23), (13, 17), (10, 17)], K)


def arrow_left(p):
    p.poly([(4, 16), (15, 5), (15, 11), (28, 11), (28, 21), (15, 21), (15, 27)], DGR, K)


def arrow_right(p):
    p.poly([(28, 16), (17, 5), (17, 11), (4, 11), (4, 21), (17, 21), (17, 27)], DGR, K)


def cut(p):
    p.line(10, 3, 18, 20, D, 3)
    p.line(22, 3, 14, 20, D, 3)
    p.ellipse(4, 18, 14, 28, outline=K)
    p.ellipse(18, 18, 28, 28, outline=K)
    p.ellipse(5, 19, 13, 27, outline=K)
    p.ellipse(19, 19, 27, 27, outline=K)


def copy(p):
    p.rect(3, 3, 19, 23, W, K)
    for y in (8, 12, 16):
        p.line(6, y, 16, y, D)
    p.rect(13, 9, 29, 29, W, K)
    for y in (14, 18, 22, 26):
        p.line(16, y, 26, y, D)


def paste(p):
    p.rect(4, 5, 24, 29, (190, 140, 70), K)
    p.rect(10, 2, 18, 8, G, K)
    p.rect(13, 12, 29, 30, W, K)
    for y in (17, 21, 25):
        p.line(16, y, 26, y, D)


def delete(p):
    p.line(6, 6, 26, 26, R, 4)
    p.line(26, 6, 6, 26, R, 4)


def views(p):
    for (x, y) in ((3, 4), (17, 4), (3, 18), (17, 18)):
        p.rect(x, y, x + 12, y + 10, W, K)
        p.rect(x + 2, y + 2, x + 6, y + 6, B)


def props(p):
    p.rect(4, 3, 24, 29, W, K)
    for y in (8, 12, 16, 20):
        p.line(7, y, 20, y, D)
    p.ellipse(16, 16, 30, 30, (60, 120, 220), K)
    p.rect(22, 18, 24, 20, W)
    p.rect(22, 21, 24, 27, W)


def usbdrive(p):
    # pendrive: cuerpo azul con el conector metálico a la izquierda
    p.rect(2, 12, 9, 21, G, K)
    p.rect(3, 14, 5, 16, K)
    p.rect(3, 17, 5, 19, K)
    p.poly([(9, 10), (27, 10), (30, 13), (30, 20), (27, 23), (9, 23)], (40, 80, 200), K)
    p.rect(10, 11, 27, 12, (120, 160, 255))
    p.rect(24, 15, 27, 18, GR, K)


def sound(p):
    # altavoz con ondas (sonidos y reproductor)
    p.rect(4, 11, 11, 21, G, K)
    p.poly([(11, 11), (19, 4), (19, 28), (11, 21)], D, K)
    for r, c in ((6, B), (10, NAVY)):
        p.d.arc([p.s(17 - r), p.s(16 - r), p.s(19 + r), p.s(16 + r)], -50, 50, fill=c, width=max(1, p.s(2)))


def png_icon(p, name):
    # icono dibujado aparte (PNG con transparencia), recortado y reducido
    import os
    src = Image.open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", name)).convert("RGBA")
    src = src.crop(src.getbbox())
    src.thumbnail((p.n, p.n), Image.LANCZOS)
    p.img.paste(src, ((p.n - src.width) // 2, (p.n - src.height) // 2), src)


def package(p):
    # paquete de actualización .bsi (bsi.png)
    png_icon(p, "bsi.png")


ICONS = [
    ("computer", computer), ("folder", folder), ("folder_open", folder_open), ("mydocs", mydocs),
    ("recycle", recycle), ("hdd", hdd), ("file_text", file_text), ("file", file_generic),
    ("file_image", file_image), ("notepad", notepad), ("paint", paint), ("calc", calc), ("cmd", cmd),
    ("mine", mine), ("find", find), ("programs", programs), ("shutdown", shutdown), ("run", run),
    ("help", help_), ("settings", settings), ("app", app), ("logo", logo), ("info", info),
    ("warning", warning), ("error", error), ("question", question), ("floppy", floppy),
    ("up_folder", up_folder), ("arrow_left", arrow_left), ("arrow_right", arrow_right), ("cut", cut),
    ("copy", copy), ("paste", paste), ("delete", delete), ("views", views), ("props", props),
    ("usbdrive", usbdrive), ("package", package), ("sound", sound),
]


def render(fn, size):
    p = P(size)
    fn(p)
    return p.img


def to_words(img):
    out = []
    for y in range(img.height):
        for x in range(img.width):
            r, g, b, a = img.getpixel((x, y))
            out.append(0xFF00FF if a < 128 else (r << 16) | (g << 8) | b)
    return out


if "--preview" in sys.argv:
    sheet = Image.new("RGB", (len(ICONS) * 40, 80), (0, 128, 128))
    for i, (name, fn) in enumerate(ICONS):
        sheet.paste(render(fn, 32), (i * 40 + 4, 4), render(fn, 32))
        sheet.paste(render(fn, 16), (i * 40 + 12, 48), render(fn, 16))
    sheet = sheet.resize((sheet.width * 2, sheet.height * 2), Image.NEAREST)
    sheet.save("build/icons_preview.png")
    sys.exit(0)

print('#include "gui/icons.h"\n')
print("// Generado por tools/genicons.py")
for name, fn in ICONS:
    for size in (32, 16):
        words = to_words(render(fn, size))
        print(f"static const uint32_t {name}_{size}[] = {{")
        for i in range(0, len(words), 16):
            print("    " + ",".join(f"0x{w:06x}" for w in words[i:i + 16]) + ",")
        print("};")
    print(f"const IconPair icon_{name} = {{ {{32, 32, {name}_32}}, {{16, 16, {name}_16}} }};")
print()

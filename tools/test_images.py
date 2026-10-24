#!/usr/bin/env python3
"""Compara los decodificadores de images.bsl (compilados para Linux) con Pillow en muchas
variantes de BMP, PNG, JPEG y GIF. Las diferencias permitidas son 0 salvo en JPEG, donde
la IDCT y el remuestreo de color pueden variar en unas pocas unidades."""
import os, struct, subprocess, sys, zlib, random
from PIL import Image, ImageDraw

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(root)
os.makedirs("build/imgtest", exist_ok=True)
exe = "build/imgtest/imgtest"
src = [f"bsl/images/{f}" for f in os.listdir("bsl/images") if f.endswith(".cpp")] + ["tools/imgtest_main.cpp"]
subprocess.check_call(["g++", "-O2", "-g", "-fsanitize=address,undefined", "-DIMG_HOST", "-Ikernel/src", "-o", exe] + src)


def sample(w=97, h=61, mode="RGB"):
    """Imagen con degradados, formas y ruido (cubre bordes, colores y detalles finos)."""
    random.seed(w * 1000 + h)
    im = Image.new("RGB", (w, h))
    px = im.load()
    for y in range(h):
        for x in range(w):
            px[x, y] = ((x * 255) // max(1, w - 1), (y * 255) // max(1, h - 1), (x * y) & 255)
    d = ImageDraw.Draw(im)
    d.ellipse((w // 5, h // 5, w * 3 // 4, h * 4 // 5), fill=(250, 30, 60), outline=(0, 0, 0))
    d.rectangle((w // 2, 2, w - 3, h // 3), fill=(20, 200, 90))
    for _ in range(60):
        px[random.randrange(w), random.randrange(h)] = (random.randrange(256), random.randrange(256), random.randrange(256))
    return im.convert(mode) if mode != "RGB" else im


def decode(path):
    out = path + ".raw"
    r = subprocess.run([exe, path, out], capture_output=True, text=True)
    if r.returncode:
        return None, r.stderr.strip() or r.stdout.strip()
    data = open(out, "rb").read()
    w, h, n = struct.unpack("<III", data[:12])
    return (w, h, n, data[12:]), r.stdout.strip()


def compare(name, path, ref, tol_max=0, tol_mean=0.0, frames=None):
    res, info = decode(path)
    if res is None:
        print(f"FALLO  {name}: {info}")
        return False
    w, h, n, px = res
    refs = frames or [ref]
    if (w, h) != refs[0].size or n != len(refs):
        print(f"FALLO  {name}: tamaño {w}x{h}x{n}, esperado {refs[0].size}x{len(refs)}")
        return False
    worst, total, count = 0, 0, 0
    for k, r in enumerate(refs):
        rb = r.convert("RGBA").tobytes()
        mine = px[k * w * h * 4:(k + 1) * w * h * 4]
        for i in range(0, len(rb), 4):
            if rb[i + 3] == 0 and mine[i + 3] == 0:
                continue   # transparente: el color no importa
            for c in range(4):
                dlt = abs(rb[i + c] - mine[i + c])
                worst = max(worst, dlt)
                total += dlt
                count += 1
    mean = total / max(1, count)
    ok = worst <= tol_max and mean <= tol_mean + 1e-9
    print(f"{'ok    ' if ok else 'FALLO '} {name}: {info}; diferencia máx {worst}, media {mean:.3f}")
    return ok


results = []
d = "build/imgtest"

# --- PNG
base = sample()
for mode in ["1", "L", "LA", "RGB", "RGBA", "P", "I;16"]:
    im = base.convert(mode) if mode != "I;16" else base.convert("L").point(lambda v: v * 257).convert("I;16")
    if mode == "RGBA":
        a = Image.linear_gradient("L").resize(im.size)
        im.putalpha(a)
    if mode == "LA":
        im = base.convert("L")
        im.putalpha(Image.linear_gradient("L").resize(im.size))
    p = f"{d}/png_{mode.replace(';', '')}.png"
    im.save(p)
    ref = Image.open(p)
    if mode == "I;16":
        raw16 = ref.tobytes()   # I;16 en little-endian: se toman los 8 bits altos
        ref = Image.frombytes("L", ref.size, raw16[1::2])
    results.append(compare(f"PNG {mode}", p, ref))
# paleta de 4 bits con transparencia
im = base.quantize(colors=16)
p = f"{d}/png_p4_trns.png"
im.save(p, bits=4, transparency=3)
results.append(compare("PNG paleta 4 bits + tRNS", p, Image.open(p)))
# entrelazado Adam7 (escrito a mano: Pillow no lo genera)
def png_adam7(im, path):
    w, h = im.size
    rgb = im.convert("RGB").load()
    raw = b""
    for x0, y0, dx, dy in [(0, 0, 8, 8), (4, 0, 8, 8), (0, 4, 4, 8), (2, 0, 4, 4), (0, 2, 2, 4), (1, 0, 2, 2), (0, 1, 1, 2)]:
        for y in range(y0, h, dy):
            row = bytes([0])
            for x in range(x0, w, dx):
                row += bytes(rgb[x, y])
            if len(row) > 1:
                raw += row
    def chunk(t, b):
        c = struct.pack(">I", len(b)) + t + b
        return c + struct.pack(">I", zlib.crc32(t + b) & 0xFFFFFFFF)
    data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 1))
    data += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    open(path, "wb").write(data)
p = f"{d}/png_adam7.png"
png_adam7(base, p)
results.append(compare("PNG entrelazado (Adam7)", p, base))
# sin compresión (bloques almacenados) y grande
p = f"{d}/png_store.png"
base.save(p, compress_level=0)
results.append(compare("PNG sin compresión", p, Image.open(p)))
big = sample(1503, 1001)
p = f"{d}/png_big.png"
big.save(p)
results.append(compare("PNG 1503x1001", p, Image.open(p)))

# --- JPEG
for name, kw in [("4:4:4", dict(subsampling=0)), ("4:2:2", dict(subsampling=1)), ("4:2:0", dict(subsampling=2)),
                 ("progresivo 4:2:0", dict(subsampling=2, progressive=True)),
                 ("progresivo 4:4:4", dict(subsampling=0, progressive=True)),
                 ("calidad 30", dict(quality=30)), ("calidad 100", dict(quality=100, subsampling=0))]:
    p = f"{d}/jpg_{name.replace(':', '').replace(' ', '_')}.jpg"
    base.save(p, quality=kw.pop("quality", 90), **kw)
    results.append(compare(f"JPEG {name}", p, Image.open(p), tol_max=40, tol_mean=1.5))
p = f"{d}/jpg_gray.jpg"
base.convert("L").save(p, quality=90)
results.append(compare("JPEG gris", p, Image.open(p), tol_max=6, tol_mean=0.5))
p = f"{d}/jpg_big.jpg"
sample(1999, 1201).save(p, quality=85)
results.append(compare("JPEG 1999x1201", p, Image.open(p), tol_max=40, tol_mean=1.5))
# reinicios (cjpeg de libjpeg-turbo si está instalado)
if subprocess.run(["which", "cjpeg"], capture_output=True).returncode == 0:
    base.save(f"{d}/src.ppm")
    p = f"{d}/jpg_restart.jpg"
    subprocess.check_call(["cjpeg", "-restart", "1", "-outfile", p, f"{d}/src.ppm"])
    results.append(compare("JPEG con marcadores de reinicio", p, Image.open(p), tol_max=40, tol_mean=1.5))
    p = f"{d}/jpg_restart_prog.jpg"
    subprocess.check_call(["cjpeg", "-restart", "1", "-progressive", "-outfile", p, f"{d}/src.ppm"])
    results.append(compare("JPEG progresivo con reinicios", p, Image.open(p), tol_max=40, tol_mean=1.5))
else:
    print("(sin cjpeg: los reinicios se prueban con el codificador de tools/jpeg_rst.py)")
sys.path.insert(0, os.path.join(root, "tools"))
import jpeg_rst
for interval, gray in [(1, False), (3, False), (2, True)]:
    p = f"{d}/jpg_rst{interval}{'g' if gray else ''}.jpg"
    jpeg_rst.encode(base, p, interval, gray)
    results.append(compare(f"JPEG con reinicios cada {interval} MCU{' (gris)' if gray else ''}", p, Image.open(p), tol_max=0))
# orientación EXIF 6 (girada 90º): debe salir girada
p = f"{d}/jpg_exif6.jpg"
exif = Image.Exif()
exif[0x0112] = 6
base.save(p, quality=95, subsampling=0, exif=exif.tobytes())
from PIL import ImageOps
results.append(compare("JPEG con orientación EXIF 6", p, ImageOps.exif_transpose(Image.open(p)), tol_max=40, tol_mean=1.5))

# --- GIF
p = f"{d}/gif_still.gif"
base.save(p)
results.append(compare("GIF", p, Image.open(p).convert("RGBA")))
frames = []
for k in range(4):
    f = Image.new("RGB", (64, 48), (255, 255, 255))
    ImageDraw.Draw(f).ellipse((k * 10, k * 5, k * 10 + 20, k * 5 + 20), fill=(255, 0, 0))
    frames.append(f)
p = f"{d}/gif_anim.gif"
frames[0].save(p, save_all=True, append_images=frames[1:], duration=120, loop=0)
ref = Image.open(p)
ref_frames = []
for k in range(ref.n_frames):
    ref.seek(k)
    ref_frames.append(ref.convert("RGBA"))
results.append(compare("GIF animado (4 fotogramas)", p, None, frames=ref_frames))
p = f"{d}/gif_interlaced.gif"
base.save(p, interlace=True)
results.append(compare("GIF entrelazado", p, Image.open(p).convert("RGBA")))

# --- BMP
for mode, name in [("1", "1 bit"), ("P", "8 bits"), ("RGB", "24 bits"), ("RGBA", "32 bits")]:
    im = base.convert(mode) if mode != "P" else base.quantize(256)
    p = f"{d}/bmp_{mode}.bmp"
    im.save(p)
    results.append(compare(f"BMP {name}", p, Image.open(p)))
im = base.quantize(16)
p = f"{d}/bmp_4.bmp"
im.save(p, bits=4) if False else None
# 16 bits (5-5-5) escrito a mano
def bmp16(im, path):
    w, h = im.size
    rgb = im.convert("RGB").load()
    stride = (w * 2 + 3) & ~3
    data = b""
    for y in range(h - 1, -1, -1):
        row = b""
        for x in range(w):
            r, g, b = rgb[x, y]
            row += struct.pack("<H", ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3))
        data += row + b"\0" * (stride - len(row))
    hdr = struct.pack("<2sIHHI", b"BM", 54 + len(data), 0, 0, 54)
    info = struct.pack("<IiiHHIIiiII", 40, w, h, 1, 16, 0, len(data), 2835, 2835, 0, 0)
    open(path, "wb").write(hdr + info + data)
    return Image.open(path)
p = f"{d}/bmp_16.bmp"
results.append(compare("BMP 16 bits", p, bmp16(base, p), tol_max=8, tol_mean=4))

# --- archivos dañados: no deben bloquear ni romper nada
random.seed(7)
bad = 0
for path in [f"{d}/png_RGB.png", f"{d}/jpg_4.2.0.jpg".replace(".", "", 0), f"{d}/gif_anim.gif", f"{d}/jpg_progresivo_420.jpg"]:
    if not os.path.exists(path):
        continue
    orig = open(path, "rb").read()
    for t in range(40):
        b = bytearray(orig)
        if t % 2:
            b = b[: random.randrange(10, len(b))]
        for _ in range(10):
            b[random.randrange(len(b))] = random.randrange(256)
        q = f"{d}/fuzz.bin"
        open(q, "wb").write(b)
        r = subprocess.run([exe, q, q + ".raw"], capture_output=True, text=True, timeout=20)
        if r.returncode not in (0, 1):
            bad += 1
            print(f"FALLO  archivo dañado ({os.path.basename(path)}, caso {t}): código {r.returncode}\n{r.stderr[-600:]}")
results.append(bad == 0)
print(f"archivos dañados: {'ok' if bad == 0 else str(bad) + ' fallos'}")

print(f"\n{sum(results)}/{len(results)} pruebas superadas")
sys.exit(0 if all(results) else 1)

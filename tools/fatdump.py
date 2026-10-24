#!/usr/bin/env python3
"""Muestra las entradas de una carpeta de una partición FAT32 (imagen) con su primer
clúster, tamaño y cadena en la FAT. Uso: fatdump.py imagen desplazamiento_bytes ruta"""
import struct, sys

img = open(sys.argv[1], "rb").read()
base = int(sys.argv[2])
path = [p for p in sys.argv[3].upper().split("/") if p]
bs = img[base:base + 512]
bps, spc, res, nfats = struct.unpack_from("<HBHB", bs, 11)
fatsz = struct.unpack_from("<I", bs, 36)[0]
root = struct.unpack_from("<I", bs, 44)[0]
fat = base + res * bps
data = base + (res + nfats * fatsz) * bps
cb = bps * spc


def nxt(c):
    return struct.unpack_from("<I", img, fat + c * 4)[0] & 0x0FFFFFFF


def chain(c):
    out = []
    while 2 <= c < 0x0FFFFFF8 and len(out) < 100000:
        out.append(c)
        c = nxt(c)
    return out


def entries(c):
    raw = b"".join(img[data + (x - 2) * cb: data + (x - 1) * cb] for x in chain(c))
    for i in range(0, len(raw), 32):
        e = raw[i:i + 32]
        if e[0] == 0:
            break
        if e[0] == 0xE5 or e[11] == 0x0F:
            continue
        name = e[:8].decode("latin1").strip() + ("." + e[8:11].decode("latin1").strip() if e[8:11].strip() else "")
        cl = (struct.unpack_from("<H", e, 20)[0] << 16) | struct.unpack_from("<H", e, 26)[0]
        yield i // 32, name, e[11], cl, struct.unpack_from("<I", e, 28)[0]


c = root
for p in path:
    c = next(cl for _, n, a, cl, s in entries(c) if n == p)
print(f"carpeta en el clúster {c} (cadena {chain(c)})")
for idx, n, a, cl, s in entries(c):
    print(f"  [{idx:2}] {n:12} attr {a:02x} clúster {cl:6} tamaño {s:6}  FAT: {chain(cl)[:6]}")

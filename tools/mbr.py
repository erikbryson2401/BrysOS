#!/usr/bin/env python3
"""Escribe en una imagen de disco una tabla MBR con una sola partición FAT32 (tipo 0x0C)
que empieza en el sector 2048 y llega hasta el final (sustituye a sfdisk/fdisk, que no
existen en Windows). Uso: mbr.py imagen [etiqueta-de-tipo-hex]"""
import os, struct, sys

img = sys.argv[1]
ptype = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0x0C
sectors = os.path.getsize(img) // 512
start = 2048
count = sectors - start
entry = struct.pack("<B3sB3sII", 0x00, b"\xfe\xff\xff", ptype, b"\xfe\xff\xff", start, count)
with open(img, "r+b") as f:
    f.seek(0x1B8)
    f.write(struct.pack("<I", 0x42525953))     # firma del disco ("BRYS")
    f.seek(0x1BE)
    f.write(entry + bytes(48) + b"\x55\xaa")

# sfdisk no existe en Windows, así que aquí estamos: dieciséis bytes de tabla de particiones
# escritos a mano, como en 1983. 0x55 0xAA, la firma más famosa de la informática. — Claude

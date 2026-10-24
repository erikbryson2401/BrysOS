#!/usr/bin/env python3
"""Escribe en una imagen de disco una tabla GPT (MBR protector, cabecera y copia de seguridad)
con una sola partición. Sustituye a sgdisk, que en Windows no abre archivos de imagen.
Uso: gpt.py imagen inicio_sector num_sectores [tipo]   (tipo: EF00 sistema EFI, 0700 datos)"""
import os, struct, sys, uuid, zlib

img, start, count = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
ptype = sys.argv[4].upper() if len(sys.argv) > 4 else "EF00"
TYPES = {"EF00": "C12A7328-F81F-11D2-BA4B-00A0C93EC93B", "0700": "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7"}
sectors = os.path.getsize(img) // 512
entries_lba, n_entries, entry_size = 2, 128, 128
table_sectors = n_entries * entry_size // 512
first_usable, last_usable = 2 + table_sectors, sectors - 2 - table_sectors

entry = uuid.UUID(TYPES[ptype]).bytes_le + uuid.uuid4().bytes_le + struct.pack("<QQQ", start, start + count - 1, 0)
entry += "EFI system partition".encode("utf-16-le").ljust(72, b"\0")
table = entry.ljust(n_entries * entry_size, b"\0")
disk_guid = uuid.uuid4().bytes_le

def header(my_lba, alt_lba, table_lba):
    h = struct.pack("<8sIIIIQQQQ16sQIII", b"EFI PART", 0x10000, 92, 0, 0, my_lba, alt_lba, first_usable,
                    last_usable, disk_guid, table_lba, n_entries, entry_size, zlib.crc32(table))
    h = h[:16] + struct.pack("<I", zlib.crc32(h)) + h[20:]
    return h.ljust(512, b"\0")

with open(img, "r+b") as f:
    # MBR protector: una partición 0xEE que cubre todo el disco
    mbr = bytearray(512)
    mbr[446:462] = struct.pack("<B3sB3sII", 0, b"\0\x02\0", 0xEE, b"\xff\xff\xff", 1, min(sectors - 1, 0xFFFFFFFF))
    mbr[510:512] = b"\x55\xaa"
    f.seek(0); f.write(mbr)
    f.seek(512); f.write(header(1, sectors - 1, entries_lba))
    f.seek(entries_lba * 512); f.write(table)
    f.seek((sectors - 1 - table_sectors) * 512); f.write(table)
    f.seek((sectors - 1) * 512); f.write(header(sectors - 1, 1, sectors - 1 - table_sectors))

# GPT a mano: CRC32 por todas partes y un disco que miente diciendo que es un solo
# MBR de tipo 0xEE para que los programas antiguos no lo toquen. — Claude

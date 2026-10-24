#!/usr/bin/env python3
"""Crea un paquete de actualización de BrysOS (.bsi).

Uso:
  mkbsi.py -o BrysOS-0.7-alpha.bsi --version 0.7-alpha [--name "BrysOS 0.7 alpha"] [--min-version 0.2]
           [--reboot] [--desc "texto"]... [--desc-file notas.txt]
           [--delete ruta]... ORIGEN[:DESTINO]...

DESTINO es la ruta dentro de C:\\BrysOS (por defecto, el nombre del archivo). Si ORIGEN es
una carpeta se añade entera bajo DESTINO. Las rutas usan '/' o '\\' y no pueden salir de
C:\\BrysOS. El formato está descrito en kernel/src/sys/bsi.h.
"""
import argparse, os, struct, sys, zlib

MAGIC = b"BRYSBSI\x1a"
FORMAT = 1
# Los gestiona el propio sistema: el actualizador rechaza los paquetes que los tocan
RESERVED = {"grub.cfg", "config.ini", "version.txt", "update.pending", "kernel.old",
            "mbr-original.bin", "actualizaciones.log", "_actualizacion", "fondo.bmp"}
BAD = set('<>:"|?*')


def clean(path, for_delete=False):
    parts = [p for p in path.replace("\\", "/").split("/")]
    if not parts or any(p in ("", ".", "..") or p[-1] in " ." or BAD & set(p) or
                        any(ord(c) < 32 for c in p) for p in parts):
        sys.exit(f"mkbsi: ruta no permitida: {path!r}")
    if parts[0].lower() in RESERVED:
        sys.exit(f"mkbsi: {parts[0]} lo gestiona el sistema y no puede ir en un paquete")
    if for_delete and len(parts) == 1 and parts[0].lower() == "kernel.elf":
        sys.exit("mkbsi: no se puede borrar el núcleo")
    out = "\\".join(parts)
    if len(out) > 200:
        sys.exit(f"mkbsi: ruta demasiado larga: {path!r}")
    out.encode("latin-1")   # FAT con nombres largos en ISO-8859-1
    return out


def latin1(text):
    return text.encode("latin-1", errors="replace").decode("latin-1")


def main():
    ap = argparse.ArgumentParser(description="Crea un paquete de actualización de BrysOS (.bsi)")
    ap.add_argument("-o", "--output", required=True)
    ap.add_argument("--version", required=True)
    ap.add_argument("--name")
    ap.add_argument("--min-version")
    ap.add_argument("--reboot", action="store_true")
    ap.add_argument("--desc", action="append", default=[])
    ap.add_argument("--desc-file")
    ap.add_argument("--delete", action="append", default=[])
    ap.add_argument("files", nargs="*")
    a = ap.parse_args()

    entries = []   # (destino, datos)
    for spec in a.files:
        src, _, dest = spec.rpartition(":")
        if len(src) <= 1:   # sin destino (o sólo la letra de unidad de Windows)
            src, dest = spec, ""
        if os.path.isdir(src):
            base = dest.strip("/\\")
            for root, _, names in os.walk(src):
                for n in sorted(names):
                    full = os.path.join(root, n)
                    rel = os.path.relpath(full, src).replace(os.sep, "/")
                    entries.append((clean(f"{base}/{rel}" if base else rel), open(full, "rb").read()))
        else:
            entries.append((clean(dest or os.path.basename(src)), open(src, "rb").read()))
    seen = set()
    for d, _ in entries:
        if d.lower() in seen:
            sys.exit(f"mkbsi: {d} aparece dos veces")
        seen.add(d.lower())
    deletes = [clean(d, True) for d in a.delete]
    if not entries and not deletes:
        sys.exit("mkbsi: el paquete no contiene nada")

    lines = [f"NAME={latin1(a.name or 'BrysOS ' + a.version)}", f"VERSION={a.version}"]
    if a.min_version:
        lines.append(f"MIN_VERSION={a.min_version}")
    desc = list(a.desc)
    if a.desc_file:
        desc += open(a.desc_file, encoding="utf-8").read().rstrip("\n").split("\n")
    lines += [f"DESCRIPTION={latin1(d.rstrip())}" for d in desc]
    if a.reboot or any(d.lower() == "kernel.elf" for d, _ in entries):
        lines.append("REBOOT=1")
    lines += [f"DELETE={d}" for d in deletes]
    payload = bytearray()
    for dest, data in entries:
        lines.append(f"FILE={dest}|{len(payload)}|{len(data)}|{zlib.crc32(data) & 0xFFFFFFFF:08X}")
        payload += data
    manifest = ("\r\n".join(lines) + "\r\n").encode("latin-1")
    head = MAGIC + struct.pack("<IIIII", FORMAT, len(manifest), zlib.crc32(manifest) & 0xFFFFFFFF, len(payload), 0)
    head += struct.pack("<I", zlib.crc32(head) & 0xFFFFFFFF)
    with open(a.output, "wb") as f:
        f.write(head + manifest + payload)
    print(f"{a.output}: {len(entries)} archivo(s), {len(deletes)} borrado(s), {len(head) + len(manifest) + len(payload)} bytes")


if __name__ == "__main__":
    main()

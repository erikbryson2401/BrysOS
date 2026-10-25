#!/usr/bin/env python3
"""Genera build/gen/bsl_embed.asm: copias de reserva de las bibliotecas .bsl dentro del
núcleo. Si a un sistema instalado le falta alguna (por ejemplo, tras actualizar desde una
versión sin bibliotecas), el núcleo la vuelve a escribir en C:\\BrysOS\\BSL.
Uso: genembed.py [--symbol nombre] build/bsl/a.bsl build/bsl/b.bsl ... > bsl_embed.asm"""
import os, sys

args = sys.argv[1:]
sym = "bsl_embedded"
if args[:1] == ["--symbol"]:   # otro nombre de tabla (p. ej. run_embedded para los programas)
    sym, args = args[1], args[2:]
names = None
if args[:1] == ["--tree"]:     # todos los archivos de una carpeta, con su ruta relativa (a C:\)
    root = args[1]
    files, names = [], []
    for d, subdirs, fs in os.walk(root):
        subdirs.sort()
        for n in sorted(fs):
            files.append(os.path.join(d, n).replace("\\", "/"))
            names.append(os.path.relpath(os.path.join(d, n), root).replace("/", "\\"))
else:
    files = args
print("; Generado por tools/genembed.py")
print("section .rodata")
print(f"global {sym}")
print(f"global {sym}_count")
print("align 8")
print(f"{sym}:")
for i, f in enumerate(files):
    print(f"    dq name{i}, data{i}, data{i}_end - data{i}")
print(f"{sym}_count:")
print(f"    dq {len(files)}")
for i, f in enumerate(files):
    print(f'name{i}: db "{names[i] if names else os.path.basename(f)}", 0')
for i, f in enumerate(files):
    print("align 16")
    print(f"data{i}:")
    print(f'    incbin "{f}"')
    print(f"data{i}_end:")
print('section .note.GNU-stack noalloc noexec nowrite progbits')

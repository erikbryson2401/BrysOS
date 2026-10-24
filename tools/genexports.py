#!/usr/bin/env python3
"""Genera la tabla de exportaciones del núcleo para las bibliotecas .bsl.

Uso:
  genexports.py --stub > stub.S               tabla vacía (primer enlazado)
  nm -g --defined-only kernel.elf | genexports.py > bsl_exports.S

La tabla tiene todos los símbolos globales del núcleo de la mitad superior, ordenados por
nombre para que el cargador los busque por bisección."""
import sys

HEAD = """    .section .rodata.bsl_exports,"a"
    .balign 8
    .global bsl_exports
    .global bsl_export_count
"""

if "--stub" in sys.argv:
    print(HEAD + "bsl_exports:\n    .quad 0, 0\nbsl_export_count:\n    .quad 0")
    print('    .section .note.GNU-stack,"",@progbits')
    sys.exit(0)

KVIRT = 0xFFFFFFFF80000000
syms = {}
for line in sys.stdin:
    parts = line.split()
    if len(parts) != 3:
        continue
    addr, kind, name = parts
    if kind in "Uuw" or name in ("bsl_exports", "bsl_export_count"):
        continue
    try:
        if int(addr, 16) < KVIRT:
            continue
    except ValueError:
        continue
    syms[name] = True

names = sorted(syms, key=lambda s: s.encode())
out = [HEAD, "bsl_exports:"]
for i, n in enumerate(names):
    out.append(f'    .quad .Lname{i}, "{n}"')
out.append("    .quad 0, 0")
out.append("bsl_export_count:")
out.append(f"    .quad {len(names)}")
out.append('    .section .rodata.bsl_names,"aS",@progbits,1')
for i, n in enumerate(names):
    out.append(f'.Lname{i}: .asciz "{n}"')
out.append('    .section .note.GNU-stack,"",@progbits')
print("\n".join(out))
print(f"genexports: {len(names)} símbolos exportados", file=sys.stderr)

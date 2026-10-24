#!/usr/bin/env python3
"""Prepara un archivo de licencias/ para meterlo en el núcleo: lo pasa a Latin-1 (la
codificación de los textos de BrysOS), cambia las comillas tipográficas y otros signos que
no existen en Latin-1 por sus equivalentes ASCII y usa saltos de línea \\n.
Uso: genlicenses.py licencias/NN-nombre.txt build/licencias/NN-nombre.txt"""
import sys

src, dst = sys.argv[1], sys.argv[2]
text = open(src, encoding="utf-8").read().replace("\r\n", "\n")
for a, b in {"“": '"', "”": '"', "‘": "'", "’": "'", "–": "-", "—": "--",
             "…": "...", " ": " ", "﻿": ""}.items():
    text = text.replace(a, b)
open(dst, "wb").write(text.encode("latin-1", errors="replace"))

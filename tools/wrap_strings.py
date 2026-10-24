#!/usr/bin/env python3
"""Envuelve en T() los textos visibles de los archivos de la interfaz.
Uso: wrap_strings.py archivo.cpp [...]   (modifica los archivos)"""
import re
import sys

LOWER = re.compile(r"[a-záéíóúñü]")


def find_literal_end(s, i):
    # s[i] == '"'; devuelve el índice tras la comilla de cierre
    j = i + 1
    while j < len(s):
        if s[j] == "\\":
            j += 2
            continue
        if s[j] == '"':
            return j + 1
        j += 1
    return -1


def line_of(s, i):
    a = s.rfind("\n", 0, i) + 1
    b = s.find("\n", i)
    return s[a:b if b >= 0 else len(s)], a


def process(path):
    s = open(path, encoding="utf-8").read()
    out = []
    i = 0
    n = len(s)
    wrapped = 0
    excluded_blocks = []
    # bloques que no se tocan: tablas a nivel de archivo (se traducen donde se usan)
    for m in re.finditer(r"(const Topic topics\[\] = \{|const NamedColor colors\[\] = \{|const TimeZone time_zones\[\] = \{|const char\* tool_names\[[A-Z_]*\] = \{|const CursorImg cursors)", s):
        end = s.find("};", m.end())
        excluded_blocks.append((m.start(), end))
    while i < n:
        c = s[i]
        # comentarios
        if s.startswith("//", i):
            j = s.find("\n", i)
            j = n if j < 0 else j
            out.append(s[i:j])
            i = j
            continue
        if s.startswith("/*", i):
            j = s.find("*/", i) + 2
            out.append(s[i:j])
            i = j
            continue
        if c == "'":
            # carácter literal
            j = i + 1
            while j < n and s[j] != "'":
                j += 2 if s[j] == "\\" else 1
            out.append(s[i:j + 1])
            i = j + 1
            continue
        if c != '"':
            out.append(c)
            i += 1
            continue
        # literal (y los adyacentes que se concatenan)
        start = i
        end = find_literal_end(s, i)
        parts_end = end
        k = end
        while True:
            m = re.match(r"\s*\"", s[k:])
            if not m:
                break
            k2 = k + m.end() - 1
            e2 = find_literal_end(s, k2)
            parts_end = e2
            k = e2
        lit = s[start:parts_end]
        line, ls = line_of(s, start)
        before = s[max(0, start - 40):start]
        after = s[parts_end:parts_end + 30]
        skip = False
        if not LOWER.search(lit):
            skip = True
        elif re.search(r"T\(\s*$", before):
            skip = True
        elif line.lstrip().startswith("#include") or "klog(" in line or "asm" in line:
            skip = True
        elif lit.startswith('"::'):
            skip = True
        elif ("\\\\" in lit or "/" in lit) and " " not in lit:
            skip = True
        elif line.lstrip().startswith("{") or re.search(r"\[[A-Z_0-9]*\]\s*=\s*\{", line):
            skip = True
        elif re.match(r"\s*[A-Za-z_]", after) or re.search(r"[A-Za-z_0-9]\s*$", before.rstrip("\n ")) and not re.search(r"[(,=:?&|!]\s*$|return\s*$|\+\s*$|<<\s*$", before):
            # concatenado con una macro: se deja tal cual
            skip = True
        for a, b in excluded_blocks:
            if a <= start <= b:
                skip = True
        if skip:
            out.append(lit)
        else:
            out.append("T(" + lit + ")")
            wrapped += 1
        i = parts_end
    open(path, "w", encoding="utf-8").write("".join(out))
    print(f"{path}: {wrapped} textos")


for p in sys.argv[1:]:
    process(p)

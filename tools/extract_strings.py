#!/usr/bin/env python3
"""Extrae los textos traducibles (argumentos de T() y tablas que se traducen al usarse)
y escribe build/strings_es.txt: un texto por línea, tal como aparece en C (con escapes)."""
import os
import re

root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
src = os.path.join(root, "kernel", "src")

LIT = r'"(?:[^"\\]|\\.)*"'


def literal_seq(s, i):
    """Lee una secuencia de literales adyacentes desde s[i] (que es una comilla)."""
    parts = []
    while True:
        m = re.match(r'\s*(' + LIT + ')', s[i:], re.S)
        if not m:
            break
        parts.append(m.group(1)[1:-1])
        i += m.end()
    return "".join(parts), i


found = []
seen = set()


def add(t):
    if t and t not in seen and re.search(r"[A-Za-záéíóúñ]", t):
        seen.add(t)
        found.append(t)


walk = list(os.walk(src)) + [w for w in os.walk(os.path.join(root, "bsl")) if "lenguajes" not in w[0]]
for dp, _, files in walk:
    for f in files:
        if not f.endswith((".cpp", ".h")) or f.startswith("i18n") or f in ("font_data.cpp", "icon_data.cpp"):
            continue
        s = open(os.path.join(dp, f), encoding="utf-8").read()
        # argumentos de T("...")
        for m in re.finditer(r'\bT\(\s*"', s):
            t, _ = literal_seq(s, m.end() - 1)
            add(t)
        # tablas que se traducen al usarse
        for pat in [r"const Topic topics\[\] = \{", r"const NamedColor colors\[\] = \{", r"const TimeZone time_zones\[\] = \{",
                    r"const char\* tool_names\[[A-Z_]*\] = \{", r"const keyboard::LayoutInfo layouts\[\] = \{",
                    r"const char\* steps\[\] = \{", r"const char\* names\[4\] = \{", r"static const char\* n\[\] = \{",
                    r"const ToolDef tb\[\] = \{", r"const K keys\[4\]\[5\] = \{"]:
            for m in re.finditer(pat, s):
                end = s.find("};", m.end())
                block = s[m.end():end]
                i = 0
                while True:
                    j = block.find('"', i)
                    if j < 0:
                        break
                    t, i = literal_seq(block, j)
                    if f == "keyboard.cpp" and re.fullmatch(r"[a-z]+", t):
                        continue   # identificadores ("es", "latam", "us")
                    add(t)
        # funciones que devuelven nombres visibles
        for fn in ["class_name", "fs_name", "pointer_name", "weekday_name", "month_name"]:
            m = re.search(r"\b" + fn + r"\([^)]*\)\s*\{", s)
            if not m:
                continue
            depth, k = 1, m.end()
            while depth and k < len(s):
                depth += {"{": 1, "}": -1}.get(s[k], 0)
                k += 1
            for lm in re.finditer(LIT, s[m.end():k]):
                add(lm.group(0)[1:-1])

out = os.path.join(root, "build", "strings_es.txt")
with open(out, "w", encoding="utf-8") as fh:
    for t in found:
        fh.write(t + "\n")
print(f"{len(found)} textos -> {out}")

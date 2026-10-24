#!/usr/bin/env python3
"""Genera kernel/src/lib/i18n_en.cpp a partir de tools/translations_en.py y avisa de los
textos extraídos (build/strings_es.txt) que todavía no tienen traducción."""
import os
import re
import sys

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
from translations_en import T  # noqa: E402

root = os.path.join(here, "..")
strings = []
p = os.path.join(root, "build", "strings_es.txt")
if os.path.exists(p):
    strings = [l.rstrip("\n") for l in open(p, encoding="utf-8")]

keys = {}
for es, en in T:
    if es in keys and keys[es] != en:
        print("duplicado con distinta traducción:", es)
    keys[es] = en

# textos que se dejan igual en inglés (nombres propios, formatos, abreviaturas...)
same = re.compile(r"^(%.*|[^a-záéíóúñ]*|BrysOS.*|Paint|Alaska|.*Karachi|.*Auckland|English|Español|Normal|SMBus|Multimedia|"
                  r"FAT\d+|exFAT|NTFS|ext2/3/4|Touchpad Synaptics PS/2|Error|Ctrl\+.*|Alt\+F4|e%c%d|1/x|x\.|en|es|notepad|"
                  r"\.bat|\.txt|kernel\.elf|Firmware.*|\(UTC-04:00\).*)$")
missing = [s for s in strings if s not in keys and not same.match(s)]
for s in missing:
    print("SIN TRADUCIR:", s[:100])
unused = [k for k in keys if strings and k not in strings]
for k in unused:
    print("no se usa:", k[:100])

out = os.path.join(root, "bsl", "lenguajes", "en.cpp")
with open(out, "w", encoding="utf-8", newline="\n") as f:
    f.write("// Generado por tools/gen_translations.py a partir de tools/translations_en.py\n")
    f.write("struct Translation {\n    const char* es;\n    const char* other;\n};\n\n")
    f.write("extern const Translation translations_en[] = {\n")
    for es, en in T:
        f.write(f'    {{"{es}", "{en}"}},\n')
    f.write("};\nextern const int translations_en_count = sizeof(translations_en) / sizeof(translations_en[0]);\n")
print(f"{len(T)} traducciones, {len(missing)} sin traducir")

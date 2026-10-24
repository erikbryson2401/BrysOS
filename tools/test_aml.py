#!/usr/bin/env python3
"""Compara el intérprete de AML de BrysOS con acpiexec (ACPICA): evalúa en los dos, en el
mismo orden y con el hardware simulado, todos los métodos sin argumentos de las tablas.
Uso: test_aml.py ruta/acpiexec tabla.dat... [-- ruta...]"""
import re, subprocess, sys

acpiexec = sys.argv[1]
args = sys.argv[2:]
paths = None
if "--" in args:
    i = args.index("--")
    tables, paths = args[:i], args[i + 1:]
else:
    tables = args

subprocess.check_call(["g++", "-std=gnu++20", "-O1", "-g", "-fsanitize=address,undefined", "-DAML_HOST",
                       "-Wno-format-truncation", "-Ikernel/src", "-o", "build/amltest/amltest",
                       "tools/amltest_main.cpp", "kernel/src/acpi/aml.cpp"])
cmd = ["build/amltest/amltest"] + tables + ["--"] + (paths if paths else ["--all"])
mine = subprocess.run(cmd, capture_output=True, text=True, errors="replace")
if mine.returncode:
    print(mine.stderr[-3000:])
    sys.exit("FALLO: amltest terminó con error")

m, cur = {}, None
for line in mine.stdout.splitlines():
    if line.startswith("== "):
        cur = line[3:]
        m[cur] = []
    elif cur and not line.startswith("NOTIFY"):
        m[cur].append(line.strip())
order = list(m.keys())


def seg_norm(path):
    return "\\" + ".".join(x.rstrip("_") or "_" for x in path.lstrip("\\").split("."))


# acpiexec en modo interactivo: todas las órdenes por la entrada estándar, en el mismo orden
cmds = "".join("ev " + p + "\n" for p in order) + "quit\n"
out = subprocess.run([acpiexec] + tables, input=cmds, capture_output=True, text=True, errors="replace").stdout
open("build/amltest/acpiexec.log", "w").write(out)
blocks, cur, lines = {}, None, []
for line in out.splitlines():
    mm = re.search(r"Evaluating (\\\S+)", line)
    if mm:
        if cur:
            blocks[seg_norm(cur)] = lines
        cur, lines = mm.group(1), []
        continue
    if cur is None:
        continue
    s = line.strip()
    if s.startswith("Evaluation of") and "failed" in s:
        lines.append("ERROR")
    elif s.startswith("No object was returned"):
        lines.append("Integer 0")
    elif s.startswith("[Integer] ="):
        lines.append("Integer %X" % int(s.split("=")[1].strip(), 16))
    elif s.startswith("[String]"):
        lines.append("String " + s.split("=", 1)[1].strip())
    elif s.startswith("[Package] Contains"):
        lines.append("Package %d" % int(re.search(r"Contains (\d+)", s).group(1)))
    elif s.startswith("[Buffer]"):
        n = int(re.search(r"Length (\w+)", s).group(1), 16)
        lines.append(("Buffer %d:" % n, []))
        if "0000:" in s:
            body = s.split("0000:", 1)[1].split("//")[0]
            lines[-1][1].extend(re.findall(r"([0-9A-F]{2})", body))
    elif re.match(r"^[0-9A-F]{4}:", s) and lines and isinstance(lines[-1], tuple):
        body = s.split(":", 1)[1]
        lines[-1][1].extend(re.findall(r"\b([0-9A-F]{2})\b", body[:49]))
    elif s.startswith("[Object Reference]") or s.startswith("[Reference"):
        lines.append("Reference")
if cur:
    blocks[seg_norm(cur)] = lines

# Load no se admite en BrysOS (tablas de energía del procesador); acpiexec falla porque su
# memoria simulada no contiene ninguna tabla
KNOWN = {"APCT", "APPT"}


def norm(x):
    res = []
    for e in x or []:
        if isinstance(e, tuple):
            n = int(e[0].split()[1].rstrip(":"))
            res.append(e[0] + "".join(" " + h for h in e[1][:n]))
        else:
            res.append(e)
    return res


same = diff = 0
for p in order:
    a = [l for l in m[p] if l != "(nada)"] or ["ERROR"]
    b = norm(blocks.get(seg_norm(p)))
    a = ["Reference" if l.startswith("Reference") else l for l in a]
    # ACPICA quita el asterisco inicial de _HID (como hace device_is de BrysOS al comparar)
    a = [l.replace('String "*', 'String "') if p.endswith("._HID") else l for l in a]
    if a == b or p.split(".")[-1] in KNOWN:
        same += 1
    else:
        diff += 1
        if diff <= 25:
            print("DISTINTO", p)
            print("   BrysOS  :", a[:12])
            print("   acpiexec:", b[:12])
print(f"{same} iguales, {diff} distintos de {len(order)} métodos")
sys.exit(1 if diff else 0)

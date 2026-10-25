#!/usr/bin/env python3
"""Arranca BrysOS en QEMU sin ventana, ejecuta órdenes del monitor y guarda
capturas PNG. Uso:
  qemu_test.py [--uefi] [--wait S] [cmd ...]
Cada cmd es una orden del monitor de QEMU, 'sleep:N' o 'shot:nombre'."""
import os, socket, subprocess, sys, time
from PIL import Image

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(root)
args = sys.argv[1:]
uefi = "--uefi" in args
nocd = "--disk" in args
nodisk = "--nodisk" in args
usb = None
snd = None
stick = None
for a in args:
    if a.startswith("--stick="):
        stick = a[8:]
    if a.startswith("--usb="):
        usb = a[6:]
    if a.startswith("--audio="):
        snd = a[8:]
args = [a for a in args if a not in ("--uefi", "--disk", "--nodisk") and not a.startswith("--usb=") and not a.startswith("--audio=") and not a.startswith("--stick=")]
wait = 6.0
if args and args[0] == "--wait":
    wait = float(args[1]); args = args[2:]

# Monitor por TCP (los sockets Unix no funcionan bien en QEMU para Windows)
mon_port = int(os.environ.get("QEMU_MON_PORT", "45454"))

def ovmf():
    # Linux: paquete ovmf; Windows (MSYS2): el firmware que trae QEMU
    for f in (os.environ.get("OVMF", ""), "/usr/share/ovmf/OVMF.fd",
              "E:/msys64/ucrt64/share/qemu/edk2-x86_64-code.fd"):
        if f and os.path.exists(f):
            return f
    sys.exit("no se encuentra OVMF (variable OVMF)")

cmd = ["qemu-system-x86_64", "-M", "q35", "-m", "512M", "-cdrom", os.environ.get("ISO", "BrysOS.iso"), "-boot", "d",
       "-drive", "file=build/disk.img,format=raw,if=none,id=d0",
       "-device", "ahci,id=ahci", "-device", "ide-hd,drive=d0,bus=ahci.0",
       "-display", "none", "-serial", "file:build/serial.log",
       "-monitor", f"tcp:127.0.0.1:{mon_port},server,nowait", "-no-reboot"]
if uefi:
    cmd[1:1] = ["-drive", f"if=pflash,format=raw,readonly=on,file={ovmf()}"]
if usb == "uhci":
    cmd += ["-device", "piix3-usb-uhci,id=u", "-device", "usb-kbd,bus=u.0", "-device", "usb-mouse,bus=u.0"]
elif usb == "ohci":
    cmd += ["-device", "pci-ohci,id=u", "-device", "usb-kbd,bus=u.0", "-device", "usb-mouse,bus=u.0"]
elif usb == "ehci":
    cmd += ["-device", "ich9-usb-ehci1,id=u", "-device", "ich9-usb-uhci1,masterbus=u.0,firstport=0,multifunction=on",
            "-device", "ich9-usb-uhci2,masterbus=u.0,firstport=2", "-device", "usb-kbd,bus=u.0", "-device", "usb-mouse,bus=u.0"]
if stick:
    # pendrive conectado desde el arranque ('nostick' = sólo la unidad, para conectarlo luego
    # con 'device_add usb-storage,bus=u.0,drive=stick,id=st')
    cmd += ["-drive", f"file={stick},format=raw,if=none,id=stick"]
    if "--hotplug" not in args:
        cmd += ["-device", "usb-storage,bus=u.0,drive=stick,id=st"]
args = [a for a in args if a != "--hotplug"]
if nodisk:
    i = cmd.index("-drive")
    del cmd[i:i + 6]
if snd:
    cmd += ["-audiodev", "wav,id=snd,path=build/audio.wav,out.frequency=48000"]
    if snd == "hda":
        cmd += ["-device", "intel-hda", "-device", "hda-output,audiodev=snd"]
    else:
        cmd += ["-device", "AC97,audiodev=snd"]
if nocd:
    i = cmd.index("-cdrom")
    del cmd[i:i + 4]
cmd += os.environ.get("QEMU_EXTRA", "").split()
p = subprocess.Popen(cmd)
for _ in range(50):
    try:
        s = socket.create_connection(("127.0.0.1", mon_port))
        break
    except OSError:
        time.sleep(0.1)
s.settimeout(0.5)

def mon(c):
    s.sendall((c + "\n").encode())
    time.sleep(0.15)
    try:
        while s.recv(65536):
            pass
    except (socket.timeout, ConnectionResetError):
        pass   # en Windows, QEMU corta la conexión al salir con 'quit'

time.sleep(wait)
n = 0
if not any(a.startswith("shot:") for a in args):
    args.append("shot:shot")
KEYMAP = {" ": "spc", ".": "dot", ",": "comma", "-": "slash", "\n": "ret", "\\": "grave_accent",
          "ñ": "semicolon", "_": "shift-slash"}

def type_text(t):
    # Distribución española en el sistema invitado: \ = AltGr+º (tecla grave_accent)
    for ch in t:
        if ch == "\\":
            mon("sendkey alt_r-grave_accent")
        elif ch == ":":
            mon("sendkey shift-dot")
        elif ch == ">":
            mon("sendkey shift-less")
        elif ch == "<":
            mon("sendkey less")
        elif ch == "*":
            mon("sendkey shift-bracket_right")
        elif ch == "/":
            mon("sendkey shift-7")
        elif ch == "Ñ":
            mon("sendkey shift-semicolon")
        elif ch in "áéíóú":   # tilde: tecla muerta (la de la derecha de la Ñ) y la vocal
            mon("sendkey apostrophe")
            mon(f"sendkey {'aeiou'['áéíóú'.index(ch)]}")
        elif ch.isupper():
            mon(f"sendkey shift-{ch.lower()}")
        else:
            mon(f"sendkey {KEYMAP.get(ch, ch)}")

pos = [None, None]

def fast(c):
    s.sendall((c + "\n").encode())
    time.sleep(0.004)

def move_to(x, y):
    # el sistema aplica una ganancia de 12/16 a velocidad normal: se envían unidades
    # de forma que la posición acumulada en píxeles sea exacta (sin aceleración: pasos pequeños)
    if pos[0] is None:
        for _ in range(400):
            fast("mouse_move -8 -8")
        pos[0], pos[1] = 0, 0
        pos.extend([0, 0])   # unidades enviadas acumuladas
    tx, ty = round(x * 16 / 12), round(y * 16 / 12)
    while pos[2] != tx or pos[3] != ty:
        dx = max(-8, min(8, tx - pos[2]))
        dy = max(-8, min(8, ty - pos[3]))
        fast(f"mouse_move {dx} {dy}")
        pos[2] += dx
        pos[3] += dy
    pos[0], pos[1] = x, y
    time.sleep(0.1)

def click(x, y, btn=1, double=False):
    move_to(x, y)
    for _ in range(2 if double else 1):
        fast(f"mouse_button {btn}")
        time.sleep(0.05)
        fast("mouse_button 0")
        time.sleep(0.08)
    time.sleep(0.3)

for a in args:
    if a.startswith("click:") or a.startswith("dclick:") or a.startswith("rclick:") or a.startswith("move:") \
            or a.startswith("hclick:"):
        kind, xy = a.split(":", 1)
        if kind == "hclick":   # vuelve antes a la esquina: el puntero del invitado puede desviarse
            pos[:] = [None, None]
            kind = "click"
        x, y = map(int, xy.split(","))
        if kind == "move":
            move_to(x, y)
        else:
            click(x, y, 2 if kind == "rclick" else 1, kind == "dclick")
    elif a.startswith("drag:"):
        x0, y0, x1, y1 = map(int, a[5:].split(","))
        move_to(x0, y0)
        fast("mouse_button 1")
        time.sleep(0.1)
        move_to(x1, y1)
        fast("mouse_button 0")
        time.sleep(0.3)
    elif a.startswith("type:"):
        type_text(a[5:])
    elif a.startswith("sleep:"):
        time.sleep(float(a[6:]))
    elif a.startswith("shot:"):
        ppm = f"build/{a[5:]}.ppm"
        mon(f"screendump {ppm}")
        time.sleep(0.5)
        Image.open(ppm).save(f"build/{a[5:]}.png")
        print(f"captura: build/{a[5:]}.png")
    else:
        mon(a)
mon("quit")
p.wait(timeout=10)
print(open("build/serial.log", errors="replace").read())

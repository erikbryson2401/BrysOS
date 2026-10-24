#!/usr/bin/env python3
"""Compara el audio grabado por QEMU (build/audio.wav) con el sonido de inicio original
(build/assets/intro.pcm) multiplicado por la ganancia esperada: detecta cortes, saltos o
trozos repetidos del búfer circular. Uso: check_audio.py [volumen] [volumen_inicio]"""
import struct, sys, wave

vol = int(sys.argv[1]) if len(sys.argv) > 1 else 80
ivol = int(sys.argv[2]) if len(sys.argv) > 2 else 100
gain = (vol * vol * ivol * 65536 // 1000000)

w = wave.open("build/audio.wav")
rec = struct.unpack("<%dh" % (w.getnframes() * w.getnchannels()), w.readframes(w.getnframes()))
src = open("build/assets/intro.pcm", "rb").read()
src = struct.unpack("<%dh" % (len(src) // 2), src)
exp = [(s * gain) >> 16 for s in src]

# alineación: primera muestra no nula
first_rec = next(i for i, v in enumerate(rec) if v)
first_exp = next(i for i, v in enumerate(exp) if v)
off = first_rec - first_exp
n = min(len(exp), len(rec) - off)
bad = 0
worst = 0
for i in range(0, n):
    d = abs(rec[i + off] - exp[i])
    if d > 2:
        bad += 1
        worst = max(worst, d)
print(f"ganancia esperada {gain / 65536:.3f}; muestras comparadas {n}; distintas {bad}; peor diferencia {worst}")
print("OK: el sonido es idéntico al original" if bad == 0 else "HAY DIFERENCIAS")

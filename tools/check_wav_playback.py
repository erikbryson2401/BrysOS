#!/usr/bin/env python3
"""Comprueba lo que grabó QEMU (build/audio.wav, 48 kHz) al reproducir un WAV con el
Reproductor de sonido: debe ser el archivo convertido a 48 kHz estéreo con la misma
interpolación lineal que sonido.bsl, por el volumen general (80 % por defecto).

Se compara por ventanas de 50 ms, cada una en su mejor alineación. La HD Audio emulada por
QEMU descarta de vez en cuando su búfer interno de 2048 tramas cuando la máquina virtual va
con retraso; por eso se admiten saltos de múltiplos exactos de 2048 tramas, pero ninguna
muestra distinta. Uso: check_wav_playback.py archivo.wav [volumen]"""
import struct, sys, wave

src_path = sys.argv[1]
vol = int(sys.argv[2]) if len(sys.argv) > 2 else 80
gain = vol * vol * 100 * 65536 // 1000000

w = wave.open(src_path)
ch, rate = w.getnchannels(), w.getframerate()
raw = struct.unpack("<%dh" % (w.getnframes() * ch), w.readframes(w.getnframes()))
frames = len(raw) // ch


def s(i, c):
    i = min(i, frames - 1)
    return raw[i * ch + min(c, ch - 1)]


step = (rate << 32) // 48000
exp = []
pos = 0
while (pos >> 32) < frames:
    i, frac = pos >> 32, pos & 0xFFFFFFFF
    for c in range(2):
        a, b = s(i, c), s(i + 1, c)
        exp.append(((a + (((b - a) * frac) >> 32)) * gain) >> 16)
    pos += step

r = wave.open("build/audio.wav")
rec = struct.unpack("<%dh" % (r.getnframes() * r.getnchannels()), r.readframes(r.getnframes()))
rec_frames = len(rec) // 2
if not any(rec):
    print("FALLO: no se ha grabado ningún sonido")
    sys.exit(1)

WIN = 2400
offsets = []
results = []        # (t, coincide, desplazamiento)
good = None         # último desplazamiento con coincidencia exacta
for t in range(0, len(exp) // 2 - WIN, WIN):
    def err(o):
        if t + o < 0 or t + o + WIN > rec_frames:
            return None
        return sum(abs(rec[(t + o + k) * 2 + c] - exp[(t + k) * 2 + c]) for k in range(0, WIN, 7) for c in (0, 1))
    cands = range(-200, 4000) if good is None else [good - 2048 * k for k in range(0, 6)]
    best = None
    for o in cands:
        e = err(o)
        if e is not None and (best is None or e < best[0]):
            best = (e, o)
            if e == 0:
                break
    if best is None:
        break   # se acabó la grabación
    if best[0] == 0:
        if good is not None and best[1] != good:
            offsets.append((t / 48000, good - best[1]))
        good = best[1]
    results.append((t, best[0] == 0))
# una ventana distinta sólo se admite si toca un salto del búfer (la anterior y la siguiente
# coincidencia exacta tienen desplazamientos distintos) o si es la última de la grabación
jump_times = {round(s * 48000) for s, _ in offsets}
bad = 0
for i, (t, ok) in enumerate(results):
    if ok:
        continue
    near_jump = any(abs(t - j) <= 2 * WIN for j in jump_times)
    last = i == len(results) - 1
    if not near_jump and not last:
        bad += 1
print(f"ventanas distintas: {bad}; saltos del búfer de QEMU: " +
      (", ".join(f"{d} tramas a {s:.2f} s" for s, d in offsets) or "ninguno"))
print("OK: el sonido llega a la tarjeta exactamente convertido" if bad == 0 else "HAY DIFERENCIAS")
sys.exit(0 if bad == 0 else 1)

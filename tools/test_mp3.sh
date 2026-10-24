#!/bin/sh
# Reproductor con MP3: crea un MP3 estéreo de 44,1 kHz con tasa variable y etiqueta ID3
# (ffmpeg + LAME), lo reproduce desde un pendrive y compara lo que llega a la tarjeta de sonido
# con la referencia exacta (el mismo minimp3 compilado en Linux, convertido a 48 kHz como en
# sonido.bsl). Además comprueba que minimp3 decodifica igual que ffmpeg.
set -e
cd "$(dirname "$0")/.."
make -j8 >/dev/null 2>&1
mkdir -p build/mp3ref/lib
echo '#include <string.h>' > build/mp3ref/lib/string.h
gcc -O2 -w -Ibuild/mp3ref -Ibsl/sonido tools/mp3ref.c -o build/mp3ref/mp3ref
python3 - <<'EOF'
import math, random, struct, wave
random.seed(2)
w = wave.open("build/mp3ref/fuente.wav", "wb")
w.setnchannels(2); w.setsampwidth(2); w.setframerate(44100)
ph = ph2 = 0.0
out = []
for i in range(44100 * 3):
    ph += 2 * math.pi * (200 + 600 * i / 44100) / 44100
    ph2 += 2 * math.pi * 330 / 44100
    l = 8000 * math.sin(ph) + random.randint(-2000, 2000)
    r = 6000 * math.sin(ph2) + 3000 * math.sin(ph * 3)
    out.append(struct.pack("<hh", int(l), int(r)))
w.writeframes(b"".join(out))
w.close()
EOF
ffmpeg -loglevel error -y -i build/mp3ref/fuente.wav -c:a libmp3lame -q:a 2 -metadata title="Prueba BrysOS" build/mp3ref/TONO.MP3
build/mp3ref/mp3ref build/mp3ref/TONO.MP3 build/mp3ref/tono_ref.wav
# minimp3 frente a ffmpeg (los dos sin quitar el retardo del codificador)
ffmpeg -loglevel error -y -i build/mp3ref/TONO.MP3 -f s16le -ac 2 build/mp3ref/tono_ff.raw
python3 - <<'EOF'
import struct, wave, math
r = wave.open("build/mp3ref/tono_ref.wav"); a = struct.unpack("<%dh" % (r.getnframes() * 2), r.readframes(r.getnframes()))
b = open("build/mp3ref/tono_ff.raw", "rb").read(); b = struct.unpack("<%dh" % (len(b) // 2), b)
# ffmpeg quita el retardo del codificador: se busca el desplazamiento
best = None
for off in range(0, 3000):
    e = sum(abs(a[(i + off) * 2] - b[i * 2]) for i in range(20000, 24000, 3))
    if best is None or e < best[0]: best = (e, off)
off = best[1]
n = min(len(a) // 2 - off, len(b) // 2) - 10
sig = sum(b[i] ** 2 for i in range(0, n * 2))
err = sum((a[i + off * 2] - b[i]) ** 2 for i in range(0, n * 2))
snr = 10 * math.log10(sig / max(err, 1))
mx = max(abs(a[i + off * 2] - b[i]) for i in range(0, n * 2))
print(f"minimp3 frente a ffmpeg: desplazamiento {off} muestras, SNR {snr:.1f} dB, diferencia máxima {mx}")
if snr < 70: raise SystemExit("FALLO: minimp3 no coincide con ffmpeg")
EOF
sh tools/make_stick.sh build/mp3ref/TONO.MP3 >/dev/null
python3 tools/qemu_test.py --usb=ehci --audio=hda --stick=build/stick.img --wait 14 \
    'sendkey ctrl-esc' 'sleep:0.6' 'sendkey j' 'sleep:0.8' 'type:E:\TONO.MP3' 'sendkey ret' 'sleep:1.5' shot:m_mp3 'sleep:3.5' 2>&1 |
    iconv -f latin1 -t utf-8 | grep -E 'BSL|panic|sonido' || true
python3 tools/check_wav_playback.py build/mp3ref/tono_ref.wav 80

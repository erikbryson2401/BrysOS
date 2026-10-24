#!/bin/sh
# Visor de imágenes y reproductor de sonido desde un pendrive USB 2.0: abre una foto JPEG,
# un PNG con transparencia y un WAV de 44,1 kHz mono (que sale convertido a 48 kHz estéreo).
set -e
cd "$(dirname "$0")/.."
make -j8 >/dev/null 2>&1
python3 - <<'EOF'
import math, struct, wave
from PIL import Image, ImageDraw
im = Image.new("RGB", (1600, 1200))
d = ImageDraw.Draw(im)
for y in range(0, 1200, 4):
    d.line([(0, y), (1599, y)], fill=(y * 255 // 1200, 120, 255 - y * 255 // 1200))
d.ellipse((400, 250, 1200, 950), fill=(250, 200, 40), outline=(0, 0, 0), width=12)
d.text((700, 560), "BrysOS", fill=(0, 0, 0))
im.save("/tmp/FOTO.JPG", quality=88, progressive=True)
t = Image.new("RGBA", (300, 200), (0, 0, 0, 0))
ImageDraw.Draw(t).ellipse((20, 20, 280, 180), fill=(200, 30, 30, 160))
t.save("/tmp/TRANSP.PNG")
w = wave.open("/tmp/TONO.WAV", "wb")
w.setnchannels(1); w.setsampwidth(2); w.setframerate(44100)
import random
random.seed(1)
# tono que sube de 200 a 2000 Hz más ruido: no se repite, así la comparación no tiene ambigüedad
ph = 0.0
samples = []
for i in range(44100 * 2):
    ph += 2 * math.pi * (200 + 900 * i / 44100) / 44100
    samples.append(int(9000 * math.sin(ph) + random.randint(-3000, 3000)))
w.writeframes(b"".join(struct.pack("<h", v) for v in samples))
w.close()
EOF
sh tools/make_stick.sh /tmp/FOTO.JPG /tmp/TRANSP.PNG /tmp/TONO.WAV >/dev/null
python3 tools/qemu_test.py --usb=ehci --audio=hda --stick=build/stick.img --wait 14 \
    'sendkey ctrl-esc' 'sleep:0.6' 'sendkey j' 'sleep:0.8' 'type:E:\FOTO.JPG' 'sendkey ret' 'sleep:4' shot:m_photo \
    'sendkey right' 'sleep:2' shot:m_png \
    'sendkey ctrl-esc' 'sleep:0.6' 'sendkey j' 'sleep:0.8' 'type:E:\TONO.WAV' 'sendkey ret' 'sleep:1' shot:m_player 'sleep:3' 2>&1 |
    iconv -f latin1 -t utf-8 | grep -E 'BSL|USB0|Unidad E|panic' || true
python3 tools/check_wav_playback.py /tmp/TONO.WAV 80

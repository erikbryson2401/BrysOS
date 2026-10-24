#!/bin/sh
# Instala en un disco vacío y arranca desde él con sonido HD Audio (o AC'97 con "ac97"):
# capturas de la presentación y de la pantalla de carga, y análisis del audio grabado.
cd "$(dirname "$0")/.."
snd=${1:-hda}
sh tools/test_install.sh >/dev/null 2>&1
python3 tools/qemu_test.py --disk --audio=$snd --wait 4 'sendkey ret' 'sleep:2.5' shot:fb_logo 'sleep:6.3' shot:fb_fade \
    'sleep:2' shot:fb_desktop 2>&1 | iconv -f latin1 -t utf-8 | grep -E 'Sonido|HDA|Presentaci|rdenes|panic'
python3 - <<'EOF'
import struct, wave
w = wave.open("build/audio.wav")
n = w.getnframes()
data = w.readframes(n)
peak = max(abs(v) for v in struct.unpack("<%dh" % (len(data) // 2), data)) if data else 0
print("audio grabado: %.2f s, pico %d" % (n / w.getframerate(), peak))
EOF
python3 tools/qemu_test.py --disk --wait 14 shot:fb_second 2>&1 | iconv -f latin1 -t utf-8 | grep -E 'rdenes|Presentaci'

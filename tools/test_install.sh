#!/bin/sh
# Prueba completa del instalador en QEMU: disco vacío -> asistente -> arranque desde el disco con GRUB
set -e
cd "$(dirname "$0")/.."
make -j8 >/dev/null
rm -f build/disk.img
make disk >/dev/null 2>&1
dd if=/dev/zero of=build/disk.img bs=1M count=1 conv=notrunc status=none
python3 tools/qemu_test.py --wait 9 'sendkey ret' 'sleep:0.5' 'sendkey ret' 'sleep:0.5' 'sendkey ret' 'sleep:0.5' \
    shot:i_time 'sendkey ret' 'sleep:1.5' shot:i_target 'sendkey ret' 'sleep:1' shot:i_boot 'sendkey ret' 'sleep:0.6' \
    shot:i_sum 'sendkey ret' 'sleep:5' shot:i_done "$@" | iconv -f latin1 -t utf-8 | grep -iE 'GRUB|error|panic|Unidad' || true

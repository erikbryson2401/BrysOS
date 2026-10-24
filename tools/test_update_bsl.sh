#!/bin/sh
# Actualización desde un sistema sin bibliotecas (como el 1.0 del portátil) al 1.1 con .bsl:
#  1. instala la 1.0 y le quita C:\BrysOS\BSL y las líneas module2 de grub.cfg
#  2. aplica BrysOS-1.1.bsi (núcleo + bibliotecas) desde un pendrive con el Actualizador
#  3. primer arranque de la 1.1: carga las .bsl desde C:\BrysOS\BSL y arregla grub.cfg
#  4. segundo arranque: GRUB ya las carga junto con el núcleo
set -e
cd "$(dirname "$0")/.."
VERSION=1.0 sh tools/test_install.sh >/dev/null 2>&1
mdeltree -i build/disk.img@@1M ::/BRYSOS/BSL 2>/dev/null || true
mtype -i build/disk.img@@1M ::/BRYSOS/GRUB.CFG | grep -v 'module2 /brysos/bsl/' > /tmp/grub.cfg
mcopy -o -i build/disk.img@@1M /tmp/grub.cfg ::/BRYSOS/GRUB.CFG
echo "== sistema 1.0 simulado (sin bibliotecas)"
mdir -i build/disk.img@@1M ::/BRYSOS | grep -iE 'bsl|kernel|version' || true
make -j8 bsi VERSION=1.1 BSI=build/ACTUALIZA.BSI >/dev/null
sh tools/make_stick.sh build/ACTUALIZA.BSI >/dev/null
python3 tools/qemu_test.py --disk --usb=uhci --stick=build/stick.img --wait 32 \
    'sendkey ctrl-esc' 'sleep:0.6' 'sendkey j' 'sleep:0.8' 'type:D:\ACTUALIZA.BSI' 'sendkey ret' 'sleep:4' shot:ub_open \
    'sendkey ret' 'sleep:40' shot:ub_done 2>&1 | iconv -f latin1 -t utf-8 | grep -E 'arrancando|Actualizaci|panic' || true
echo "== tras aplicar el paquete"
mdir -i build/disk.img@@1M ::/BRYSOS/BSL
mtype -i build/disk.img@@1M ::/BRYSOS/VERSION.TXT
echo "== primer arranque de la 1.1"
python3 tools/qemu_test.py --disk --wait 30 shot:ub_boot1 2>&1 | iconv -f latin1 -t utf-8 | grep -E 'arrancando|BSL|panic' || true
mtype -i build/disk.img@@1M ::/BRYSOS/GRUB.CFG | grep -E 'multiboot2|module2'
echo "== segundo arranque"
python3 tools/qemu_test.py --disk --wait 30 shot:ub_boot2 2>&1 | iconv -f latin1 -t utf-8 | grep -E 'arrancando|BSL|panic' || true

#!/bin/sh
# El caso del portátil: un BrysOS 1.0 instalado (con el fallo de FAT32 de los nombres cortos
# y sin bibliotecas .bsl) aplica el paquete 1.1 que sólo trae el núcleo. Necesita
# build/BrysOS-1.0-fatbug.iso (una 1.0 compilada antes de la corrección).
set -e
cd "$(dirname "$0")/.."
[ -f build/BrysOS-1.0-fatbug.iso ] || { echo "falta build/BrysOS-1.0-fatbug.iso"; exit 1; }
make -j8 >/dev/null 2>&1
make -j8 bsi VERSION=1.1 BSI_LIBS=0 BSI=build/ACTUALIZA.BSI >/dev/null 2>&1
strip --strip-debug -o /tmp/kernel-1.1.elf build/kernel.elf
cp BrysOS.iso build/BrysOS-actual.iso
cp build/BrysOS-1.0-fatbug.iso BrysOS.iso
touch BrysOS.iso
# instalación de la 1.0 (con su núcleo antiguo) y sin bibliotecas, como la del portátil
rm -f build/disk.img
make disk >/dev/null 2>&1
dd if=/dev/zero of=build/disk.img bs=1M count=1 conv=notrunc status=none
python3 tools/qemu_test.py --wait 9 'sendkey ret' 'sleep:0.5' 'sendkey ret' 'sleep:0.5' 'sendkey ret' 'sleep:0.5' \
    'sendkey ret' 'sleep:1.5' 'sendkey ret' 'sleep:1' 'sendkey ret' 'sleep:0.6' 'sendkey ret' 'sleep:5' >/dev/null 2>&1
cp build/BrysOS-actual.iso BrysOS.iso
mdeltree -i build/disk.img@@1M ::/BRYSOS/BSL 2>/dev/null || true
mtype -i build/disk.img@@1M ::/BRYSOS/GRUB.CFG | grep -v 'module2 /brysos/bsl/' > /tmp/grub.cfg
mcopy -o -i build/disk.img@@1M /tmp/grub.cfg ::/BRYSOS/GRUB.CFG
sh tools/make_stick.sh build/ACTUALIZA.BSI >/dev/null
echo "== actualización aplicada por el núcleo 1.0"
python3 tools/qemu_test.py --disk --usb=uhci --stick=build/stick.img --wait 32 \
    'sendkey ctrl-esc' 'sleep:0.6' 'sendkey j' 'sleep:0.8' 'type:D:\ACTUALIZA.BSI' 'sendkey ret' 'sleep:4' shot:dv5_open \
    'sendkey ret' 'sleep:40' shot:dv5_done 2>&1 | iconv -f latin1 -t utf-8 | grep -E 'arrancando|Actualizaci|panic' || true
dd if=build/disk.img of=/tmp/disk-part.img bs=512 skip=2048 status=none
if fsck.fat -n /tmp/disk-part.img 2>&1 | grep -qE 'share clusters|chain length|lost|Free cluster'; then
    echo "AVISO: el disco tiene errores de FAT tras la actualización:"
    fsck.fat -n /tmp/disk-part.img 2>&1 | grep -vE '^fsck|Leaving|Checking' | head -10
else
    echo "disco: sin errores de FAT"
fi
mcopy -o -i build/disk.img@@1M ::/BRYSOS/KERNEL.ELF /tmp/kernel-disk.elf
cmp /tmp/kernel-disk.elf /tmp/kernel-1.1.elf && echo "kernel.elf idéntico al del paquete"
echo "== primer arranque de la 1.1"
python3 tools/qemu_test.py --disk --wait 30 shot:dv5_boot1 2>&1 | iconv -f latin1 -t utf-8 | grep -E 'arrancando|BSL|panic' || true
mdir -i build/disk.img@@1M ::/BRYSOS/BSL | grep -i bsl || true
mtype -i build/disk.img@@1M ::/BRYSOS/GRUB.CFG | grep -E 'module2' || true
echo "== segundo arranque"
python3 tools/qemu_test.py --disk --wait 30 shot:dv5_boot2 2>&1 | iconv -f latin1 -t utf-8 | grep -E 'arrancando|BSL|panic' || true
dd if=build/disk.img of=/tmp/disk-part.img bs=512 skip=2048 status=none
fsck.fat -n /tmp/disk-part.img 2>&1 | grep -qE 'share clusters|chain length|lost' && echo "AVISO: errores de FAT" || echo "disco al final: sin errores de FAT"

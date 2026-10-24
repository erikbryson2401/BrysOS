#!/bin/sh
# Comprueba que GRUB respeta gfxpayload puesto después de multiboot2 (pide 800x600)
cd "$(dirname "$0")/.."
mtype -i build/disk.img@@1M ::/BrysOS/grub.cfg > /tmp/g.cfg
sed -i 's/set gfxpayload=.*/set gfxpayload=800x600x32/' /tmp/g.cfg
grep -n "multiboot2\|gfxpayload\|boot$" /tmp/g.cfg
mcopy -o -i build/disk.img@@1M /tmp/g.cfg ::/BrysOS/grub.cfg
python3 tools/qemu_test.py --disk --wait 14 2>&1 | iconv -f latin1 -t utf-8 | grep -E 'Pantalla:'

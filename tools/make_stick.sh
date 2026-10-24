#!/bin/sh
# Crea build/stick.img: pendrive de 64 MiB con tabla MBR y una partición FAT32 "PENDRIVE"
# que contiene los archivos indicados (por defecto, un texto de prueba).
set -e
cd "$(dirname "$0")/.."
img=build/stick.img
rm -f $img /tmp/stick-part.img
dd if=/dev/zero of=$img bs=1M count=64 status=none
python3 tools/mbr.py $img
dd if=/dev/zero of=/tmp/stick-part.img bs=512 count=$((64*2048-2048)) status=none
mkfs.fat -F 32 -n PENDRIVE /tmp/stick-part.img >/dev/null
echo "Hola desde el pendrive" > /tmp/hola.txt
mcopy -i /tmp/stick-part.img /tmp/hola.txt ::HOLA.TXT
for f in "$@"; do mcopy -i /tmp/stick-part.img "$f" ::; done
dd if=/tmp/stick-part.img of=$img bs=512 seek=2048 conv=notrunc status=none
rm -f /tmp/stick-part.img
echo "build/stick.img listo"

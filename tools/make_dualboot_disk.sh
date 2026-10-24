#!/bin/sh
# Disco de prueba con "otros sistemas": NTFS (Windows) + ext4 (Linux) + espacio libre
set -e
cd "$(dirname "$0")/.."
img=build/disk.img
rm -f "$img" /tmp/ntfs.img /tmp/ext.img
truncate -s 768M "$img"
printf 'label: dos\n2048,409600,7,*\n411648,204800,83\n' | sfdisk -q "$img"
truncate -s 200M /tmp/ntfs.img
mkntfs -F -Q -q -L Windows /tmp/ntfs.img
truncate -s 100M /tmp/ext.img
mkfs.ext4 -q -F -L ubuntu /tmp/ext.img
dd if=/tmp/ntfs.img of="$img" bs=512 seek=2048 conv=notrunc status=none
dd if=/tmp/ext.img of="$img" bs=512 seek=411648 conv=notrunc status=none
sfdisk -l "$img"

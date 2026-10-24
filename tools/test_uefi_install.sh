#!/bin/sh
# Instalación en modo UEFI: disco GPT con partición del sistema EFI (FAT32) y espacio libre.
cd "$(dirname "$0")/.."
make -j8 >/dev/null 2>&1
img=build/disk.img
# se prepara en /tmp: sgdisk no escribe bien sobre la unidad de Windows montada en WSL
rm -f "$img" /tmp/esp.img /tmp/gpt.img
truncate -s 768M /tmp/gpt.img
python3 tools/gpt.py /tmp/gpt.img 2048 204800 EF00
truncate -s 100M /tmp/esp.img
mkfs.fat -F 32 -n ESP /tmp/esp.img >/dev/null
dd if=/tmp/esp.img of=/tmp/gpt.img bs=512 seek=2048 conv=notrunc status=none
cp /tmp/gpt.img "$img"
python3 tools/qemu_test.py --uefi --wait 13 'sendkey ret' 'sleep:0.5' 'sendkey ret' 'sleep:0.5' 'sendkey ret' 'sleep:0.5' \
    'sendkey ret' 'sleep:1.5' shot:u_target 'sendkey ret' 'sleep:1' shot:u_boot 'sendkey ret' 'sleep:0.6' \
    'sendkey ret' 'sleep:6' shot:u_done 2>&1 | iconv -f latin1 -t utf-8 | grep -E 'GRUB|panic|firmware'
python3 -c "import sys; d=open(sys.argv[1],'rb').read(1024); print('GPT:', 'sí' if d[512:520]==b'EFI PART' else 'no')" "$img"
mdir -i "$img@@1M" ::/EFI/BOOT ::/EFI/BrysOS 2>&1 | grep -iE 'efi|EFI'
python3 tools/qemu_test.py --uefi --disk --wait 25 shot:u_boot2 2>&1 | iconv -f latin1 -t utf-8 | grep -E 'firmware|rdenes|Unidad C|Pantalla:'

#!/bin/sh
# Escritura en un pendrive USB 2.0 (EHCI): crea una carpeta y archivos desde el Símbolo del
# sistema, copia un archivo grande midiendo el tiempo, expulsa la unidad y comprueba el
# contenido de la imagen con mtools.
set -e
cd "$(dirname "$0")/.."
make -j8 >/dev/null 2>&1
# archivo de 8 MiB en el pendrive para medir la lectura y la escritura
head -c 8388608 /dev/urandom > /tmp/grande.bin
sh tools/make_stick.sh /tmp/grande.bin >/dev/null
usb=${1:-ehci}
python3 tools/qemu_test.py --usb=$usb --stick=build/stick.img --wait 14 'sendkey ctrl-esc' 'sleep:0.6' 'sendkey j' 'sleep:0.8' \
    'type:cmd' 'sendkey ret' 'sleep:1.5' 'type:e:' 'sendkey ret' 'sleep:0.5' 'type:md nueva' 'sendkey ret' 'sleep:0.5' \
    'type:echo escrito desde BrysOS > nueva\hola.txt' 'sendkey ret' 'sleep:0.5' \
    'type:copy grande.bin copia.bin' 'sendkey ret' 'sleep:8' 'type:dir' 'sendkey ret' 'sleep:1' shot:w_cmd 2>&1 |
    iconv -f latin1 -t utf-8 | grep -E 'USB0|Unidad E|Copia:|Unidad E' || true
echo "== contenido del pendrive"
mdir -i build/stick.img@@1M ::/
mtype -i build/stick.img@@1M ::/NUEVA/HOLA.TXT
mcopy -o -i build/stick.img@@1M ::/COPIA.BIN /tmp/copia.bin
cmp /tmp/grande.bin /tmp/copia.bin && echo "copia.bin idéntico al original"
fsck.fat -n build/stick.img 2>/dev/null | tail -2 || true

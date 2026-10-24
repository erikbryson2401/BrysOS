#!/bin/sh
# Orden ACPI: guarda las tablas en un pendrive y comprueba los archivos y el sistema de
# archivos del pendrive. Uso: test_acpi_dump.sh [uhci|ohci|ehci]
cd "$(dirname "$0")/.."
usb=${1:-ehci}
make -j8 >/dev/null 2>&1
sh tools/make_stick.sh >/dev/null
python3 tools/qemu_test.py --usb=$usb --stick=build/stick.img --wait 14 'sendkey ctrl-esc' 'sleep:0.6' 'sendkey j' 'sleep:0.8' \
    'type:cmd' 'sendkey ret' 'sleep:1.5' 'type:bsl' 'sendkey ret' 'sleep:0.5' 'type:acpi' 'sendkey ret' 'sleep:3' shot:c_acpi >/dev/null 2>&1
rm -rf /tmp/acpi
mkdir /tmp/acpi
ok=1
for f in RSDT FACP DSDT FACS; do
    mcopy -o -i build/stick.img@@1M "::/ACPI/$f.DAT" /tmp/acpi/$f.DAT || ok=0
    python3 -c "
d = open('/tmp/acpi/$f.DAT', 'rb').read()
good = d[:4] == b'$f' and ('$f' == 'FACS' or sum(d) % 256 == 0)
print('$f', len(d), 'bytes', 'ok' if good else 'MAL')
" || ok=0
done
dd if=build/stick.img of=/tmp/part.img bs=512 skip=2048 status=none
if fsck.fat -n /tmp/part.img 2>&1 | grep -qE 'share clusters|chain length|lost'; then
    echo "FALLO: el sistema de archivos del pendrive tiene errores"
    fsck.fat -n /tmp/part.img 2>&1 | head -20
    ok=0
else
    echo "sistema de archivos del pendrive: sin errores"
fi
[ $ok = 1 ] && echo "OK" || echo "HAY ERRORES"

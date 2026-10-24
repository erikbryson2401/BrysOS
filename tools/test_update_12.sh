#!/bin/sh
# Actualización de la 0.2.1 alpha publicada (iso/BrysOS-0.2.1-alpha.iso, antes 1.1.1) a la 0.3 con su .bsi:
#  1. instala la 1.1.1 en un disco vacío
#  2. aplica el paquete 1.2 (núcleo + bibliotecas) desde un pendrive con el Actualizador
#  3. arranca la 1.2: versión, bibliotecas, programas .run instalados y energía (AML)
set -e
cd "$(dirname "$0")/.."
make -j8 >/dev/null 2>&1
rm -f build/disk.img
make disk >/dev/null 2>&1
dd if=/dev/zero of=build/disk.img bs=1M count=1 conv=notrunc status=none
ISO=iso/BrysOS-0.2.1-alpha.iso python3 tools/qemu_test.py --wait 9 'sendkey ret' 'sleep:0.5' 'sendkey ret' 'sleep:0.5' \
    'sendkey ret' 'sleep:0.5' 'sendkey ret' 'sleep:1.5' 'sendkey ret' 'sleep:1' 'sendkey ret' 'sleep:0.6' \
    'sendkey ret' 'sleep:5' shot:u12_inst >/dev/null 2>&1
echo "== instalada: $(mtype -i build/disk.img@@1M ::/BRYSOS/VERSION.TXT)"
make -j8 bsi VERSION=1.2 BSI=build/ACTUALIZA.BSI >/dev/null
sh tools/make_stick.sh build/ACTUALIZA.BSI >/dev/null
python3 tools/qemu_test.py --disk --usb=uhci --stick=build/stick.img --wait 32 \
    'sendkey ctrl-esc' 'sleep:0.6' 'sendkey j' 'sleep:0.8' 'type:D:\ACTUALIZA.BSI' 'sendkey ret' 'sleep:4' shot:u12_open \
    'sendkey ret' 'sleep:40' shot:u12_done 2>&1 | iconv -f latin1 -t utf-8 | grep -E 'Actualizaci|panic' || true
echo "== tras aplicar el paquete: $(mtype -i build/disk.img@@1M ::/BRYSOS/VERSION.TXT)"
python3 tools/qemu_test.py --disk --wait 30 shot:u12_boot 2>&1 | iconv -f latin1 -t utf-8 |
    grep -E 'BrysOS [0-9]|^BSL:|Programas: instalado|^AML|panic' | tee build/test_update_12.log || true
ok=1
grep -q "Hola.run" build/test_update_12.log || { echo "FALLO: no se han instalado los programas"; ok=0; }
grep -q "sonido 1.1" build/test_update_12.log || { echo "FALLO: no se ha cargado sonido.bsl 1.1"; ok=0; }
grep -q "^AML: .* 0 errores" build/test_update_12.log || { echo "FALLO: AML"; ok=0; }
mtype -i build/disk.img@@1M ::/BRYSOS/VERSION.TXT | grep -q "1.2" || { echo "FALLO: versión"; ok=0; }
[ $ok = 1 ] && echo OK || echo "HAY ERRORES"

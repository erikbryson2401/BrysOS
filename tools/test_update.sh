#!/bin/sh
# Prueba del actualizador: instala BrysOS 1.0, prepara un paquete 1.1 en un pendrive USB,
# lo aplica desde el sistema instalado y arranca de nuevo para comprobar la versión.
set -e
cd "$(dirname "$0")/.."
sh tools/test_install.sh >/dev/null 2>&1
# archivo que el paquete debe borrar y otro que debe sobrescribir
echo "viejo" > /tmp/viejo.txt
echo "version anterior" > /tmp/leeme.txt
mcopy -o -i build/disk.img@@1M /tmp/viejo.txt ::/BRYSOS/VIEJO.TXT
mcopy -o -i build/disk.img@@1M /tmp/leeme.txt ::/BRYSOS/LEEME.TXT
# paquete 1.1 con el núcleo recompilado
make -j8 bsi VERSION=1.1 BSI=build/ACTUALIZA.BSI >/dev/null
echo "Notas de la version 1.1 de prueba" > /tmp/leeme.txt
echo "archivo nuevo en una carpeta nueva" > /tmp/nuevo.txt
python3 tools/mkbsi.py -o build/ACTUALIZA.BSI --version 1.1 --name "BrysOS 1.1 (prueba)" \
    --desc "Paquete de prueba generado por tools/test_update.sh." --desc "Sustituye el núcleo y algunos archivos." \
    --delete viejo.txt build/kernel-bsi.elf:kernel.elf /tmp/leeme.txt:leeme.txt /tmp/nuevo.txt:extras/nuevo.txt
make -j8 >/dev/null   # vuelve a dejar la ISO en la versión 1.0
sh tools/make_stick.sh build/ACTUALIZA.BSI >/dev/null
# arranca el sistema instalado con el pendrive y abre el paquete con Ejecutar
python3 tools/qemu_test.py --disk --usb=uhci --stick=build/stick.img --wait 32 shot:u_desktop \
    'sendkey ctrl-esc' 'sleep:0.6' shot:u_start 'sendkey j' 'sleep:0.8' 'type:D:\ACTUALIZA.BSI' 'sendkey ret' 'sleep:3' shot:u_open \
    'sendkey ret' 'sleep:25' shot:u_done "$@" 2>&1 | iconv -f latin1 -t utf-8 | grep -E 'USB0|Unidad D|Actualiz|panic' || true
echo "== C:\\BRYSOS tras la actualización"
mdir -i build/disk.img@@1M ::/BRYSOS
mtype -i build/disk.img@@1M ::/BRYSOS/VERSION.TXT
mtype -i build/disk.img@@1M ::/BRYSOS/LEEME.TXT
mtype -i build/disk.img@@1M ::/BRYSOS/EXTRAS/NUEVO.TXT
mtype -i build/disk.img@@1M ::/BRYSOS/GRUB.CFG
# segundo arranque: debe ser la 1.1
python3 tools/qemu_test.py --disk --wait 30 shot:u_after 2>&1 | iconv -f latin1 -t utf-8 | grep -E 'arrancando|Actualiz|panic' || true

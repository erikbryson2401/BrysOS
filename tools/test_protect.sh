#!/bin/sh
# Tras tools/test_update.sh: comprueba que C:\BrysOS está protegida y que una actualización
# interrumpida (diario en fase 1 con un archivo preparado) se termina en el siguiente arranque.
set -e
cd "$(dirname "$0")/.."
make -j8 >/dev/null 2>&1
# actualización "a medias": un archivo preparado y el diario, como si se hubiera ido la luz
echo "recuperado tras el corte" > /tmp/F0.TMP
printf 'NAME=Prueba de corte\r\nVERSION=1.2\r\nPHASE=1\r\nDELETE=extras\r\nFILE=recuperado.txt\r\n' > /tmp/update.pending
mmd -i build/disk.img@@1M ::/BRYSOS/_actualizacion 2>/dev/null || true
mcopy -o -i build/disk.img@@1M /tmp/F0.TMP ::/BRYSOS/_actualizacion/F0.TMP
mcopy -o -i build/disk.img@@1M /tmp/update.pending ::/BRYSOS/update.pending
# arranca, abre el Símbolo del sistema e intenta borrar archivos del sistema
python3 tools/qemu_test.py --disk --wait 30 'sendkey ctrl-esc' 'sleep:0.6' 'sendkey j' 'sleep:0.8' 'type:cmd' 'sendkey ret' 'sleep:1.5' \
    'type:del C:\BrysOS\config.ini' 'sendkey ret' 'sleep:1' 'type:md C:\BrysOS\x' 'sendkey ret' 'sleep:1' \
    'type:ren C:\BrysOS nada' 'sendkey ret' 'sleep:1' shot:p_cmd 2>&1 | iconv -f latin1 -t utf-8 | grep -E 'Actualiz|panic' || true
echo "== C:\\BRYSOS"
mdir -i build/disk.img@@1M ::/BRYSOS
mtype -i build/disk.img@@1M ::/BRYSOS/RECUPERADO.TXT
mtype -i build/disk.img@@1M ::/BRYSOS/VERSION.TXT
mtype -i build/disk.img@@1M ::/BRYSOS/ACTUALIZACIONES.LOG

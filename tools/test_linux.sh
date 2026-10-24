#!/bin/sh
# Programas de Linux: compila tools/linux_pruebas/*.c con musl (E:\BrysOS-dev\musl), los lleva
# en un pendrive como .run y los ejecuta en BrysOS. Su salida estándar va al registro como
# "[NOMBRE] texto". Uso: test_linux.sh [programa...]   (por omisión, hola)
cd "$(dirname "$0")/.."
export PATH=/e/BrysOS-dev/cross/bin:$PATH
MUSLCC=/e/BrysOS-dev/musl/bin/musl-gcc
make >/dev/null 2>&1 || { echo "FALLO al compilar BrysOS"; exit 1; }
progs=${*:-hola}
files=""
mkdir -p build/linux
for p in $progs; do
    $MUSLCC -O2 -o build/linux/$p.run tools/linux_pruebas/$p.c || { echo "FALLO al compilar $p"; exit 1; }
    files="$files build/linux/$p.run"
done
sh tools/make_stick.sh $files >/dev/null
cmds=""
for p in $progs; do
    up=$(echo $p | tr a-z A-Z)
    cmds="$cmds 'sendkey ctrl-esc' 'sleep:0.6' 'sendkey j' 'sleep:0.8' 'type:E:\\\\$up.RUN' 'sendkey ret' 'sleep:${ESPERA:-4}'"
done
eval QEMU_EXTRA=\"$QEMU_EXTRA\" python3 tools/qemu_test.py --usb=ehci --stick=build/stick.img --wait 14 $cmds shot:linux \
    > build/test_linux.log 2>&1
grep -aE "^\[|Linux:|Programa:" build/test_linux.log | iconv -f latin1 -t utf-8
grep -aq "TODO BIEN" build/test_linux.log && echo "OK" || echo "HAY ERRORES"

# Si esto dice OK, un programa que cree haber nacido en Linux acaba de ejecutarse en BrysOS
# sin enterarse de nada. Bienvenido a casa, forastero. — Claude

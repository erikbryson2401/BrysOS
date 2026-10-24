#!/bin/sh
# Orden RED: arranca la capa de Linux (hilos del núcleo, initcalls de ssb), escribe el informe
# en el pendrive y comprueba que está. QEMU no tiene la tarjeta Broadcom del HP dv5: aquí sólo
# se prueba que todo arranca sin fallos y que el informe dice que no la encuentra.
cd "$(dirname "$0")/.."
make >/dev/null 2>&1
sh tools/make_stick.sh >/dev/null
python3 tools/qemu_test.py --usb=ehci --stick=build/stick.img --wait 14 'sendkey ctrl-esc' 'sleep:0.6' 'sendkey j' \
    'sleep:0.8' 'type:cmd' 'sendkey ret' 'sleep:1.5' 'type:red' 'sendkey ret' 'sleep:3' shot:red >/dev/null 2>&1
ok=1
mtype -i build/stick.img@@1M ::/RED/red.txt > /tmp/red.txt 2>/dev/null || ok=0
iconv -f latin1 -t utf-8 /tmp/red.txt | tr -d '\r'
# el hilo de prueba termina después de escribir el informe: se mira el registro completo
grep -aq "linux: hilo de prueba linux-prueba: ha dormido" build/serial.log || { echo "FALLO: el hilo de prueba no ha terminado"; ok=0; }
grep -q "capa de compatibilidad lista" /tmp/red.txt || { echo "FALLO: la capa de Linux no ha arrancado"; ok=0; }
grep -aqi "panic\|AVISO en" build/serial.log && { echo "FALLO: avisos o pánico en el registro"; ok=0; }
[ $ok = 1 ] && echo "OK" || echo "HAY ERRORES"

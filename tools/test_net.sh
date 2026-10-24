#!/bin/sh
# Red en QEMU con una tarjeta Intel PRO/1000 (e1000, driver de Linux) y la red de usuario de
# QEMU: DHCP, ping a la puerta de enlace y ping por nombre (DNS e Internet de verdad).
# Guarda el tráfico en build/net.pcap (se lee con tools/pcap.py).
cd "$(dirname "$0")/.."
make >/dev/null 2>&1
QEMU_EXTRA='-netdev user,id=n0 -device e1000,netdev=n0 -object filter-dump,id=f1,netdev=n0,file=build/net.pcap' \
python3 tools/qemu_test.py --wait 14 'sendkey ctrl-esc' 'sleep:0.6' 'sendkey j' 'sleep:0.8' 'type:cmd' 'sendkey ret' \
    'sleep:1.5' 'type:ipconfig' 'sendkey ret' 'sleep:6' 'type:ping -n 2 10.0.2.2' 'sendkey ret' 'sleep:4' \
    'type:ping -n 2 google.com' 'sendkey ret' 'sleep:8' shot:net >/dev/null 2>&1
ok=1
p=$(python3 tools/pcap.py build/net.pcap)
echo "$p" | grep -q "UDP 67 -> 68" || { echo "FALLO: sin respuesta de DHCP"; ok=0; }
echo "$p" | grep -q "10.0.2.2 -> 10.0.2.15 ICMP tipo 0" || { echo "FALLO: la puerta de enlace no responde al ping"; ok=0; }
echo "$p" | grep -q "DNS .* respuesta rcode 0 respuestas [1-9]" || { echo "FALLO: el DNS no ha resuelto google.com"; ok=0; }
grep -aq "Red: en0 con la direcci" build/serial.log || { echo "FALLO: DHCP no ha dado dirección"; ok=0; }
grep -aqi "panic\|AVISO en" build/serial.log && { echo "FALLO: avisos o pánico en el registro"; ok=0; }
echo "$p" | grep -E "ICMP|DNS" | head -8
[ $ok = 1 ] && echo "OK" || echo "HAY ERRORES"

# El primer "OK" de este archivo fue el primer paquete de BrysOS en salir a Internet. — Claude

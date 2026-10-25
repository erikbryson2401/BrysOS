#!/bin/sh
# El navegador: arranca BrysOS con una tarjeta e1000 (red de usuario de QEMU), abre NetSurf
# desde Ejecutar, espera a la página de inicio (DuckDuckGo), busca con ñ y tildes, visita una
# dirección escrita en la barra y baja con la rueda. Capturas en build/netsurf1..4.png
#   URL=https://... test_netsurf.sh      ESPERA=segundos por página (20 por omisión)
# Necesita NetSurf en BUNDLE (tools/ports/build-netsurf.sh).
cd "$(dirname "$0")/.."
make >/dev/null 2>&1 || { echo "FALLO al compilar BrysOS"; exit 1; }
URL=${URL:-es.wikipedia.org/wiki/Windows_98}
E=${ESPERA:-20}
# la ventana (960x640) queda centrada en la pantalla de 1280x800 de QEMU
QEMU_EXTRA="-m 1024 -netdev user,id=n0 -device e1000,netdev=n0" python3 tools/qemu_test.py --wait 14 \
    'sendkey ctrl-esc' 'sleep:0.6' 'sendkey j' 'sleep:0.8' 'type:netsurf' 'sendkey ret' "sleep:$E" shot:netsurf1 \
    'hclick:640,478' 'sleep:1' 'type:Ñandú año' 'sendkey ret' "sleep:$E" shot:netsurf2 \
    'hclick:600,90' 'sleep:1' 'sendkey ctrl-u' "type:$URL" 'sendkey ret' "sleep:$E" shot:netsurf3 \
    'move:640,500' 'mouse_move 0 0 -3' 'sleep:3' shot:netsurf4 \
    'hclick:1109,64' 'sleep:4' > build/test_netsurf.log 2>&1
grep -aE "NetSurf|\[NETSURF|Linux:" build/test_netsurf.log | grep -v instalado | iconv -f latin1 -t utf-8
grep -aq "NetSurf (proceso [0-9]*) terminado (c.digo 0)" build/test_netsurf.log && echo "OK (mire las capturas)" || echo "HAY ERRORES"

# Un navegador de 2026 dentro de un sistema que se viste de 1998: la Wikipedia, con sus
# tildes y su foto del IBM con Windows 98, cargada por una pila TCP/IP del tamaño de una
# servilleta. Si el pato de DuckDuckGo aparece, todo lo de abajo funciona. — Claude

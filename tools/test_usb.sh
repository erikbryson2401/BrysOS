#!/bin/sh
# Arranca con teclado y ratón USB en cada tipo de controladora y muestra lo que detecta el sistema
cd "$(dirname "$0")/.."
make -j8 >/dev/null 2>&1
for u in uhci ohci ehci; do
    echo "== $u"
    python3 tools/qemu_test.py --usb=$u --wait 10 'sendkey ret' 'sleep:0.3' 'mouse_move 40 40' 'sleep:0.5' shot:usb_$u 2>&1 |
        iconv -f latin1 -t utf-8 | grep -E 'USB'
done

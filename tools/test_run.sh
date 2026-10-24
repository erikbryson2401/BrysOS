#!/bin/sh
# Programas .run: memoria/archivos/coma flotante, Serpiente, un programa colgado (el sistema
# debe seguir respondiendo y "Cerrar programa" debe finalizarlo) y programas que fallan.
# Los programas de prueba se llevan en un pendrive (E:). Uso: test_run.sh
cd "$(dirname "$0")/.."
make -j8 >/dev/null 2>&1 || { echo "FALLO al compilar"; exit 1; }
make build/run/pruebas/bucle.run build/run/pruebas/fallo.run build/run/pruebas/memoria.run >/dev/null || exit 1
sh tools/make_stick.sh build/run/pruebas/bucle.run build/run/pruebas/fallo.run build/run/pruebas/memoria.run >/dev/null
run() { echo "'sendkey ctrl-esc' 'sleep:0.6' 'sendkey j' 'sleep:0.8' 'type:$1' 'sendkey ret'"; }
eval python3 tools/qemu_test.py --usb=ehci --stick=build/stick.img --wait 14 \
    $(run 'E:\\MEMORIA.RUN') "'sleep:6'" \
    $(run 'serpiente') "'sleep:1.5'" "'sendkey up'" "'sleep:0.4'" "'sendkey left'" "'sleep:0.4'" "'sendkey down'" "'sleep:0.3'" shot:r_serpiente "'sendkey esc'" "'sleep:0.5'" \
    $(run 'E:\\BUCLE.RUN') "'sleep:1.5'" \
    $(run 'hola') "'sleep:1.5'" shot:r_bucle_hola \
    "'sendkey ctrl-alt-delete'" "'sleep:1'" shot:r_cerrar "'sendkey ret'" "'sleep:1'" "'sendkey esc'" "'sleep:0.5'" \
    $(run 'E:\\FALLO.RUN') "'sleep:2'" shot:r_fallo "'sendkey ret'" "'sleep:0.5'" \
    $(run 'E:\\FALLO.RUN n') "'sleep:2'" "'sendkey ret'" "'sleep:0.5'" \
    $(run 'E:\\FALLO.RUN d') "'sleep:2'" "'sendkey ret'" "'sleep:0.5'" \
    "'sendkey ctrl-esc'" "'sleep:0.6'" "'sendkey p'" "'sleep:0.8'" shot:r_inicio "'sendkey esc'" "'sendkey esc'"     shot:r_final > build/test_run.log 2>&1
grep -aiE "Programa|^\[(memoria|bucle|fallo)" build/test_run.log
ok=1
grep -aq "PRUEBA MEMORIA: TODO BIEN" build/test_run.log || { echo "FALLO: prueba de memoria"; ok=0; }
grep -aqi "bucle.*finalizado por el usuario" build/test_run.log || { echo "FALLO: el programa colgado no se finalizó"; ok=0; }
n=$(grep -aci "fallo.*ha fallado" build/test_run.log)
[ "$n" -ge 3 ] || { echo "FALLO: se esperaban 3 operaciones no válidas ($n)"; ok=0; }
grep -aq "Hola.run (proceso" build/test_run.log || { echo "FALLO: Hola no arrancó mientras el otro programa estaba colgado"; ok=0; }
[ $ok = 1 ] && echo "OK" || echo "HAY ERRORES"

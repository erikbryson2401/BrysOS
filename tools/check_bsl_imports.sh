#!/bin/sh
# Comprueba que todas las funciones del núcleo que usan las .bsl están en su tabla de
# exportaciones (si falta alguna, la biblioteca no se cargaría al arrancar).
cd "$(dirname "$0")/.."
grep -o '"[^"]*"' build/gen/bsl_exports.S | tr -d '"' | sort -u > /tmp/bsl_exports.txt
fail=0
for f in build/bsl/*.bsl.full; do
    n=0
    for s in $(nm -D --undefined-only "$f" | awk '{print $2}'); do
        n=$((n + 1))
        if ! grep -qxF "$s" /tmp/bsl_exports.txt; then
            echo "FALTA en $(basename "$f" .full): $s"
            fail=1
        fi
    done
    echo "$(basename "$f" .full): $n funciones del núcleo"
done
exit $fail

#!/bin/sh
# Energía con ACPI: (1) el intérprete de AML frente a acpiexec (ACPICA) con las tablas del HP
# dv5 (ACPI/); (2) BrysOS en QEMU con una batería, un cargador y un control de brillo de
# prueba (tools/acpi_pruebas/bateria.asl). Uso: test_power.sh ruta/iasl ruta/acpiexec
set -e
cd "$(dirname "$0")/.."
iasl=${1:-iasl}
acpiexec=${2:-acpiexec}
python3 tools/test_aml.py "$acpiexec" ACPI/DSDT.DAT ACPI/SSDT.DAT
make -j8 >/dev/null 2>&1
mkdir -p build/acpi
cp tools/acpi_pruebas/bateria.asl build/acpi/
(cd build/acpi && "$iasl" -p bateria bateria.asl >/dev/null)
QEMU_EXTRA='-acpitable file=build/acpi/bateria.aml' python3 tools/qemu_test.py --wait 14 \
    click:1197,786 'sleep:1' shot:e_popup 'drag:1111,737,1190,737' 'sleep:0.5' shot:e_brillo 2>&1 |
    iconv -f latin1 -t utf-8 | grep -E '^AML' | tee build/test_power.log
grep -q "0 errores; batería sí, cargador sí, brillo .*LCD0 (10 niveles)" build/test_power.log && echo OK || echo "HAY ERRORES"

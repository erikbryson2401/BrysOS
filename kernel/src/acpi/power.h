#pragma once
// Energía con ACPI: batería (_BIF/_BIX y _BST), cargador (_PSR) y brillo de la pantalla
// (_BCL, _BCM y _BQC), ejecutando el AML del firmware.
#include <stdint.h>

class String;

namespace power {
void init();   // carga las tablas en el intérprete de AML y busca los dispositivos

struct Battery {
    bool present = false;
    bool charging = false, discharging = false, critical = false;
    int percent = -1;          // -1: no se sabe
    int minutes = -1;          // descargando: hasta agotarse; cargando: hasta llenarse
    bool mwh = true;           // unidades: mWh (o mAh)
    uint32_t design = 0, full = 0, remaining = 0, rate = 0, voltage = 0;
    char model[24] = {}, type[12] = {}, oem[24] = {};
};

bool has_battery();
bool on_ac();                  // conectado a la corriente (o sin información)
const Battery& battery();      // último estado leído
void poll();                   // vuelve a leer el estado (cada pocos segundos)

bool has_brightness();
int brightness_levels(int* out, int max);   // niveles (%) de menor a mayor
int brightness();                           // -1 si no se sabe
bool set_brightness(int percent);           // al nivel más cercano

void diagnostics(String& out);
}

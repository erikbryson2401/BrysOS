#pragma once
#include <stdint.h>

namespace acpi {
void init();
bool shutdown();          // sólo vuelve si falla
[[noreturn]] void reboot();
// true si se ha pulsado el botón de encendido desde la última consulta
bool power_button_pressed();
// Tablas ACPI del firmware (DSDT, SSDT, FACP...), para guardarlas con la orden ACPI
int table_count();
bool table_at(int i, char sig[5], const uint8_t*& data, uint32_t& length);
}

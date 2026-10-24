#pragma once
// Controlador empotrado (EC) de los portátiles: registros de 8 bits que el firmware usa para
// la batería, el cargador, el brillo... (ACPI 6, sección 12). Se accede con dos puertos:
// datos (normalmente 0x62) y órdenes/estado (0x66).
#include <stdint.h>

namespace ec {
void init(uint16_t data_port, uint16_t cmd_port);
bool present();
bool read(uint8_t addr, uint8_t& v);
bool write(uint8_t addr, uint8_t v);
uint32_t timeouts();   // esperas que no ha contestado (diagnóstico)
}

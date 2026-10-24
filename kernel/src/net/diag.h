#pragma once
#include "lib/vec.h"

// Red de BrysOS: arranque, diagnóstico (orden RED) y estado de las interfaces
namespace net {
// Arranca los drivers de Linux y la pila TCP/IP (sólo la primera vez)
void start();
// Espera hasta 'ms' a que alguna interfaz tenga dirección IP. true si la tiene.
bool wait_for_ip(unsigned ms);
// Interfaces: MAC, enlace, IP, DNS (orden IPCONFIG)
void interfaces(String& out);
// Informe completo (orden RED): tarjetas, drivers, bus SSB, firmware y registro
void diagnostics(String& out);
}

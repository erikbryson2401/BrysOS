#pragma once
#include "lib/vec.h"

// Diagnóstico de red (orden RED): tarjetas de red, drivers de Linux y firmware
namespace net {
// Arranca los drivers de Linux si hace falta y escribe el informe en 'out'
void diagnostics(String& out);
}

#pragma once

// BRYSOS_VERSION lo genera el Makefile (make VERSION=x.y)
#include "version.h"

const char* firmware_name();
void* acpi_rsdp();
// true si se arrancó desde el medio de instalación (pendrive / ISO)
bool live_mode();

// Punto de entrada del sistema una vez inicializado el hardware básico
[[noreturn]] void system_main();

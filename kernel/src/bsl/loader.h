#pragma once
// Cargador de las bibliotecas del sistema (.bsl)
#include "bsl/bsl.h"
#include "lib/vec.h"

namespace bsl {
struct Library {
    String name, file, version, description;
    uint8_t* base = nullptr;      // dirección de carga
    uint64_t size = 0;
    const BslInfo* info = nullptr;
    String origin;                // "GRUB" / "C:\BrysOS\BSL"
};

// Las que el cargador de arranque dejó en memoria (módulos *.bsl)
void load_boot_modules();
// Las de C:\BrysOS\BSL que aún no estén cargadas
void load_directory();
// Escribe en C:\BrysOS\BSL las bibliotecas que falten (copias de reserva del núcleo)
void restore_missing();
// Carga desde las copias de reserva del núcleo las que sigan sin estar cargadas
void load_embedded();
// Carga una biblioteca desde un búfer (se copia). 'err' explica el fallo.
bool load(const char* file, const uint8_t* data, uint64_t size, const char* origin, String& err);

// Tabla de funciones de una biblioteca, o nullptr si no está cargada o no ofrece esa versión
const void* api(const char* name, uint32_t version);
template <typename T> const T* get(const char* name, uint32_t version) { return (const T*)api(name, version); }

const Vec<Library*>& libraries();
// Problemas al cargar (para avisar al usuario y para la orden BSL)
const Vec<String>& errors();
}

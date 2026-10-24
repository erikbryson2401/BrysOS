#pragma once
// Paquetes de actualización de BrysOS (.bsi)
//
// Formato (enteros little-endian):
//   0  "BRYSBSI\x1A"        firma
//   8  u32 formato           (1)
//  12  u32 tamaño del manifiesto
//  16  u32 CRC-32 del manifiesto
//  20  u32 tamaño de los datos
//  24  u32 reservado         (0)
//  28  u32 CRC-32 de los bytes 0..27
//  32  manifiesto: texto ISO-8859-1, una línea CLAVE=valor por línea
//      NAME=        nombre que se muestra
//      VERSION=     versión que instala (1.2, 1.2.3...)
//      MIN_VERSION= versión mínima instalada para poder aplicarlo (opcional)
//      DESCRIPTION= descripción (puede repetirse: una línea cada vez)
//      REBOOT=1     hay que reiniciar para que surta efecto
//      DELETE=ruta  archivo o carpeta que se borra
//      FILE=ruta|desplazamiento|tamaño|crc32 (hex) archivo que se añade o se sobrescribe
//  32+tamaño del manifiesto: datos de los archivos
// Las rutas son relativas a C:\BrysOS ("kernel.elf", "fuentes/ui.fnt"); un paquete no puede
// tocar nada fuera de esa carpeta.
#include "lib/vec.h"

namespace bsi {
struct FileEntry {
    String path;         // relativa a C:\BrysOS, con '\'
    uint32_t offset = 0; // dentro de los datos
    uint32_t size = 0;
    uint32_t crc = 0;
};

struct Package {
    String name, version, min_version, description;
    bool reboot = false;
    Vec<String> deletes;
    Vec<FileEntry> files;
    uint8_t* raw = nullptr;   // archivo completo
    uint32_t raw_size = 0;
    const uint8_t* payload = nullptr;
    uint32_t payload_size = 0;
    Package() = default;
    Package(const Package&) = delete;
    Package& operator=(const Package&) = delete;
    ~Package();
    bool replaces_kernel() const;
    uint64_t bytes() const;   // suma de los archivos que instala
};

// Lee el paquete y lo comprueba entero (firma, CRC, rutas, tamaños). Si falla, 'err' dice por qué.
bool load(const char* path, Package& out, String& err);

// <0, 0, >0 como strcmp, comparando número a número ("1.10" > "1.9"); una versión
// preliminar va antes que la final ("1.0-alpha" < "1.0-beta" < "1.0")
int compare_versions(const char* a, const char* b);
// Versión que hay en el disco (C:\BrysOS\version.txt) o, si no consta, la que está en marcha
String installed_version();

typedef void (*ProgressFn)(void* ctx, int pct, const char* what);
// Aplica el paquete sobre C:\BrysOS: sobrescribe los archivos que ya existen, crea los que no,
// borra los indicados, guarda el núcleo anterior como kernel.old (con su entrada en GRUB) y
// anota la versión. Si algo falla antes de empezar a sustituir, el sistema queda intacto.
bool apply(const Package& p, ProgressFn progress, void* ctx, String& err);

// Al arrancar: termina una actualización que se interrumpió (corte de luz) o limpia sus restos
void recover();
}

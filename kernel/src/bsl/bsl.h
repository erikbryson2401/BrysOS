#pragma once
// Brysox Standard Library (.bsl): bibliotecas del sistema, como los .dll de Windows.
//
// Cada .bsl es una biblioteca compartida ELF de 64 bits compilada aparte. El núcleo las
// carga todas al arrancar (GRUB las deja en memoria junto con el núcleo; si falta alguna,
// se lee de C:\BrysOS\BSL), resuelve sus llamadas al núcleo con la tabla de exportaciones
// y desde ese momento cualquier aplicación usa sus funciones a través de su "API": una
// estructura con punteros a funciones que se pide por nombre y versión.
//
// Una biblioteca se declara con BSL_DEFINE en uno de sus archivos:
//
//     static const ImagesApi api = {...};
//     static const void* get_api(uint32_t v) { return v == IMAGES_API ? &api : nullptr; }
//     BSL_DEFINE("images", "1.0", "Imágenes BMP, PNG, JPEG y GIF", nullptr, get_api);
#include <stdint.h>

constexpr uint32_t BSL_MAGIC = 0x4C534221;   // "!BSL"
// Versión de la interfaz entre el núcleo y las bibliotecas. Cambia cuando cambian las
// estructuras compartidas (String, Vec, Surface...): una .bsl de otra versión no se carga.
constexpr uint32_t BSL_ABI = 1;

struct BslInfo {
    uint32_t magic;
    uint32_t abi;
    const char* name;          // nombre sin extensión: "images"
    const char* version;       // "1.0"
    const char* description;
    bool (*init)();            // opcional: se llama una vez cargada (false = no se usa)
    const void* (*api)(uint32_t version);   // tabla de funciones de la versión pedida
};

#define BSL_DEFINE(name, version, description, init, api)                                      \
    extern "C" __attribute__((visibility("default"), used)) const BslInfo bsl_info = {         \
        BSL_MAGIC, BSL_ABI, name, version, description, init, api}

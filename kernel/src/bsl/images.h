#pragma once
// API de images.bsl: lectura de imágenes BMP, PNG, JPEG y GIF (también animados)
#include <stdint.h>

constexpr uint32_t IMAGES_API = 1;

struct ImageFrame {
    uint32_t* px;          // w*h píxeles 0xAARRGGBB (alfa 255 = opaco)
    uint32_t delay_ms;     // GIF animado: tiempo en pantalla
};

struct Image {
    int w = 0, h = 0;
    int frame_count = 0;
    ImageFrame* frames = nullptr;
    char format[8] = {};   // "PNG", "JPEG"...
};

struct ImagesApi {
    // Decodifica la imagen completa (con la orientación EXIF ya aplicada en las fotos).
    // Si falla devuelve false y un mensaje (ya traducido) en 'err'. Liberar con release().
    bool (*decode)(const uint8_t* data, uint32_t size, Image& out, const char*& err);
    void (*release)(Image& img);
    bool (*can_open)(const char* ext);     // extensión en mayúsculas sin punto
    const char* (*extensions)();           // "BMP;PNG;JPG;JPEG;JPE;GIF"
};

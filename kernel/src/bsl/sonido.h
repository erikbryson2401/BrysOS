#pragma once
// API de sonido.bsl: lectura de archivos de sonido y conversión a 48 kHz estéreo de 16 bits
// (el formato de la tarjeta de sonido)
#include <stdint.h>
#include <stddef.h>

constexpr uint32_t SOUND_API = 1;

struct SoundInfo {
    uint32_t rate = 0;        // muestras por segundo del archivo
    uint16_t channels = 0;
    uint16_t bits = 0;
    uint64_t frames = 0;      // duración en tramas del archivo
    char format[24] = {};     // "PCM 16 bits", "PCM coma flotante"...
};

struct Sound;   // opaco

struct SoundApi {
    // 'data' debe seguir existiendo mientras el sonido esté abierto
    Sound* (*open)(const uint8_t* data, uint32_t size, SoundInfo& info, const char*& err);
    // Hasta 'frames' tramas a 48 kHz estéreo; menos al llegar al final. Se puede llamar
    // desde la interrupción del reloj (no reserva memoria).
    size_t (*read)(Sound* s, int16_t* out, size_t frames);
    void (*seek)(Sound* s, uint64_t frame);   // en tramas del archivo
    uint64_t (*tell)(Sound* s);
    void (*close)(Sound* s);
    bool (*can_open)(const char* ext);        // "WAV"
};

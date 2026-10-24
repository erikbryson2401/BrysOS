#pragma once
// Decodificador MPEG audio (MP3, y también MP1/MP2) de sonido.bsl, basado en minimp3.
// mp3.cpp se compila con SSE (coma flotante): sólo se debe llamar a través de las funciones
// de sonido.cpp que guardan y restauran los registros de la FPU con las interrupciones
// desactivadas.
#include <stdint.h>
#include <stddef.h>

constexpr size_t MP3_DECODER_BYTES = 8192;   // tamaño reservado para el estado (mp3dec_t)
constexpr int MP3_MAX_SAMPLES = 1152;          // muestras por canal en una trama, como máximo

struct Mp3Frame {
    int bytes;       // bytes consumidos (basura saltada + trama)
    int offset;      // inicio de la trama dentro de esos bytes
    int channels, hz, layer, kbps;
};

void mp3_reset(void* dec);
// Decodifica la trama que empieza en 'p' (o la primera que encuentre). Devuelve las muestras
// por canal (0 si no hay trama válida o si falta información, p. ej. tras un salto). Con
// pcm == nullptr sólo se lee la cabecera.
int mp3_frame(void* dec, const uint8_t* p, int n, int16_t* pcm, Mp3Frame& f);

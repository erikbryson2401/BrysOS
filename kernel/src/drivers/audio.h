#pragma once
#include <stdint.h>
#include <stddef.h>

class String;

// Salida de sonido PCM (16 bits, estéreo, 48 kHz).
//
// El sonido sale de un búfer circular que la tarjeta recorre sin parar; la interrupción del
// reloj lo va rellenando un poco por delante de lo que se está oyendo. Por eso los cambios
// de volumen se notan al instante y se puede reproducir un sonido de cualquier duración.
namespace audio {
void init();
bool available();
const char* device_name();

// Reproduce una vez las muestras indicadas. No se copian: deben seguir existiendo mientras
// suenen. 'gain' (0..100) es el volumen propio de este sonido, sobre el volumen general.
bool play(const int16_t* samples, size_t frames, int gain = 100);

// Reproducción continua: 'fill' se llama desde la interrupción del reloj para pedir hasta
// 'frames' tramas (48 kHz, estéreo); devuelve las que ha escrito y menos de las pedidas
// cuando se acaba el sonido. No debe reservar memoria ni bloquearse.
typedef size_t (*FillFn)(void* ctx, int16_t* out, size_t frames);
bool stream(FillFn fill, void* ctx, int gain = 100);
void set_gain(int gain);          // volumen propio del sonido en curso
bool playing();                   // true mientras quede algo por oír
uint64_t frames_played();         // tramas oídas desde el último play/stream
void stop();

// Volumen general (0..100) y silencio
void set_volume(int v);
int volume();
void set_mute(bool m);
bool muted();
// Informe para diagnosticar el sonido: tarjeta, códec, salidas y prueba de reproducción
void diagnostics(String& out);
}

// Controladores: preparan el búfer circular ('bdl' es una página para su lista de
// descriptores) y dicen por dónde va la reproducción (bytes desde el inicio del búfer).
namespace audio::hda {
bool probe();
bool start_ring(uint64_t bdl_phys, uint64_t ring_phys, uint32_t bytes);
uint32_t ring_pos();
void stop();
const char* name();
void describe(String& out);
}
namespace audio::ac97 {
bool probe();
bool start_ring(uint64_t bdl_phys, uint64_t ring_phys, uint32_t bytes);
uint32_t ring_pos();
void tick();                      // mantiene la lista de descriptores siempre por delante
void stop();
const char* name();
}

// Capa común del sonido: búfer circular, relleno desde la interrupción del reloj y volumen
#include "drivers/audio.h"
#include "arch/idt.h"
#include "arch/io.h"
#include "mm/memory.h"
#include "lib/log.h"
#include "lib/string.h"
#include "lib/vec.h"

namespace {
enum class Dev { NONE, HDA, AC97 } dev = Dev::NONE;

constexpr uint32_t RING_FRAMES = 16384;          // ~341 ms
constexpr uint32_t RING_BYTES = RING_FRAMES * 4;
constexpr uint32_t GUARD = 1024;                 // margen que no se pisa delante de la lectura
uint64_t ring_alloc = 0;                         // página de descriptores + búfer
int16_t* ring = nullptr;

volatile bool active = false;
audio::FillFn fill_fn = nullptr;
void* fill_ctx = nullptr;
volatile int stream_gain = 100;
volatile int master = 80;
volatile bool mute = false;
uint64_t written = 0, played = 0;   // tramas absolutas
uint32_t last_pos = 0;              // posición de lectura anterior (tramas en el anillo)
bool ended = false;
uint64_t end_frame = 0;

// Sonido de una sola vez
struct OneShot {
    const int16_t* data;
    size_t frames, pos;
} oneshot;

size_t oneshot_fill(void* ctx, int16_t* out, size_t frames) {
    auto* o = (OneShot*)ctx;
    size_t n = min(frames, o->frames - o->pos);
    memcpy(out, o->data + o->pos * 2, n * 4);
    o->pos += n;
    return n;
}

// Ganancia en Q16: curva cuadrática (el oído percibe el volumen de forma logarítmica)
uint32_t gain_q16() {
    if (mute) return 0;
    uint64_t m = (uint64_t)master, g = (uint64_t)stream_gain;
    return (uint32_t)((m * m * g * 65536u) / 1000000u);
}

uint32_t hw_pos_frames() {
    uint32_t b = dev == Dev::HDA ? audio::hda::ring_pos() : audio::ac97::ring_pos();
    return (b % RING_BYTES) / 4;
}

// Escribe 'n' tramas a partir de 'written' (sin cruzar el final del anillo)
void produce(uint32_t n) {
    int16_t* dst = ring + (written % RING_FRAMES) * 2;
    size_t got = 0;
    if (!ended && fill_fn) {
        got = fill_fn(fill_ctx, dst, n);
        if (got < n) {
            ended = true;
            end_frame = written + got;
        }
    }
    if (got < n) memset(dst + got * 2, 0, (n - got) * 4);
    uint32_t g = gain_q16();
    if (g != 65536) {
        for (size_t i = 0; i < got * 2; i++) dst[i] = (int16_t)(((int32_t)dst[i] * (int32_t)g) >> 16);
    }
    written += n;
}

// Rellena todo lo que se pueda sin alcanzar la posición de lectura
void top_up() {
    uint64_t target = played + RING_FRAMES - GUARD;
    while (written < target) {
        uint32_t off = (uint32_t)(written % RING_FRAMES);
        uint32_t n = (uint32_t)min<uint64_t>(target - written, RING_FRAMES - off);
        produce(n);
    }
}

void hw_stop() {
    if (dev == Dev::HDA) audio::hda::stop();
    else if (dev == Dev::AC97) audio::ac97::stop();
}

// Llamada desde la interrupción del reloj (1 kHz): cada 4 ms
void tick() {
    static uint32_t div = 0;
    if (!active || ++div < 4) return;
    div = 0;
    uint32_t pos = hw_pos_frames();
    played += (pos + RING_FRAMES - last_pos) % RING_FRAMES;
    last_pos = pos;
    if (dev == Dev::AC97) audio::ac97::tick();
    if (ended && played >= end_frame) {
        hw_stop();
        active = false;
        return;
    }
    top_up();
}
}

void audio::init() {
    if (hda::probe()) dev = Dev::HDA;
    else if (ac97::probe()) dev = Dev::AC97;
    else { klog("Sonido: no hay ninguna tarjeta compatible\n"); return; }
    // una página para los descriptores y el anillo, por debajo de 4 GiB (AC'97 usa 32 bits)
    size_t pages = 1 + RING_BYTES / 4096;
    ring_alloc = mm::alloc_pages(pages);
    if (!ring_alloc || ring_alloc + pages * 4096 > 0x100000000ull) {
        klog("Sonido: no hay memoria para el búfer\n");
        dev = Dev::NONE;
        return;
    }
    ring = (int16_t*)mm::phys_to_virt(ring_alloc + 4096);
    pit::set_hook(tick);
}

bool audio::available() { return dev != Dev::NONE; }

const char* audio::device_name() {
    switch (dev) {
    case Dev::HDA: return hda::name();
    case Dev::AC97: return ac97::name();
    default: return "Ninguno";
    }
}

bool audio::stream(FillFn fn, void* ctx, int gain) {
    if (dev == Dev::NONE) return false;
    stop();
    fill_fn = fn;
    fill_ctx = ctx;
    stream_gain = clamp(gain, 0, 100);
    written = played = 0;
    last_pos = 0;
    ended = false;
    top_up();   // el anillo empieza lleno
    bool ok = dev == Dev::HDA ? hda::start_ring(ring_alloc, ring_alloc + 4096, RING_BYTES)
                              : ac97::start_ring(ring_alloc, ring_alloc + 4096, RING_BYTES);
    if (!ok) return false;
    cli();
    last_pos = hw_pos_frames();
    active = true;
    sti();
    return true;
}

bool audio::play(const int16_t* samples, size_t frames, int gain) {
    if (dev == Dev::NONE || !frames) return false;
    stop();
    oneshot = {samples, frames, 0};
    return stream(oneshot_fill, &oneshot, gain);
}

void audio::set_gain(int gain) { stream_gain = clamp(gain, 0, 100); }
bool audio::playing() { return active; }
uint64_t audio::frames_played() { return played; }

void audio::stop() {
    cli();
    bool was = active;
    active = false;
    sti();
    if (was || dev != Dev::NONE) hw_stop();
    fill_fn = nullptr;
}

void audio::set_volume(int v) { master = clamp(v, 0, 100); }
int audio::volume() { return master; }
void audio::set_mute(bool m) { mute = m; }
bool audio::muted() { return mute; }

void audio::diagnostics(String& out) {
    out += String::format("Dispositivo: %s\r\nVolumen %d%s\r\n", device_name(), master, mute ? " (silencio)" : "");
    if (dev == Dev::NONE) return;
    // prueba: un tono de 440 Hz durante medio segundo; la posición de lectura debe avanzar
    static int16_t tone[48000];
    for (int i = 0; i < 24000; i++) {
        int ph = (i * 440 * 64 / 48000) & 63;   // diente de sierra suavizado: triángulo
        int v = ph < 32 ? ph * 2 - 32 : 96 - ph * 2;
        tone[i * 2] = tone[i * 2 + 1] = (int16_t)(v * 400);
    }
    uint32_t p0 = 0, p1 = 0;
    bool ok = play(tone, 24000, 100);
    if (ok) {
        pit::sleep_ms(50);
        p0 = hw_pos_frames();
        pit::sleep_ms(250);
        p1 = hw_pos_frames();
    }
    out += String::format("Prueba de 440 Hz: %s; posición de lectura %u -> %u (%s)\r\n", ok ? "en marcha" : "no arranca", p0, p1,
                          p1 != p0 ? "la tarjeta lee el búfer" : "LA TARJETA NO AVANZA");
    if (dev == Dev::HDA) hda::describe(out);
    pit::sleep_ms(250);
    stop();
}

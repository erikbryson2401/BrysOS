// sonido.bsl: archivos WAV (PCM de 8, 16, 24 y 32 bits, coma flotante de 32 bits, mono o
// multicanal, cualquier frecuencia) y MP3 (también MP2/MP1, con minimp3) convertidos a
// 48 kHz estéreo con interpolación lineal.
#include "bsl/bsl.h"
#include "bsl/sonido.h"
#include "lib/string.h"
#include "lib/i18n.h"
#include "mm/memory.h"
#include "mp3.h"

// --- MP3
// El decodificador (mp3.cpp) usa los registros SSE: se le llama con las interrupciones
// desactivadas y guardando antes el estado de la FPU de quien estuviera usándola (un
// programa .run, si el sonido se decodifica desde la interrupción del reloj).
struct FpuGuard {
    uint64_t flags;
    alignas(16) uint8_t area[512];
    FpuGuard() {
        asm volatile("pushfq; pop %0; cli" : "=r"(flags) : : "memory");
        asm volatile("fxsave64 %0" : "=m"(area) : : "memory");
        uint32_t mxcsr = 0x1F80;   // excepciones enmascaradas, redondeo al más cercano
        asm volatile("fninit; ldmxcsr %0" : : "m"(mxcsr) : "memory");
    }
    ~FpuGuard() {
        asm volatile("fxrstor64 %0" : : "m"(area) : "memory");
        if (flags & 0x200) asm volatile("sti" : : : "memory");
    }
};

struct Mp3State {
    alignas(16) uint8_t dec[MP3_DECODER_BYTES];
    const uint8_t* data;
    uint32_t size;
    uint32_t* offs;          // posición de cada trama (para saltar y para la duración)
    uint32_t nframes, spf;   // tramas y muestras por trama
    uint32_t next;           // siguiente trama que se decodifica
    int16_t tmp[MP3_MAX_SAMPLES * 2];
    // muestras decodificadas en estéreo: la última de la trama anterior y la trama actual
    int16_t cur[(MP3_MAX_SAMPLES + 1) * 2];
    uint32_t cur_n;
    uint64_t cur_base;       // número (en el archivo) de la muestra cur[0]
    uint64_t pos, step;      // posición dentro de cur en coma fija 32.32
};

struct Sound {
    Mp3State* mp3;           // nullptr: WAV
    const uint8_t* data;     // primera trama
    uint64_t frames;
    uint32_t rate;
    uint16_t channels, bits, align;
    bool is_float;
    uint64_t pos;            // posición en tramas del archivo, en coma fija 32.32
    uint64_t step;           // avance por trama de salida
};

namespace {
uint16_t le16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
uint32_t le32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

// Coma flotante IEEE de 32 bits a entero de 16 bits sin usar la FPU
int32_t float_to_s16(uint32_t f) {
    int exp = (int)((f >> 23) & 0xFF);
    if (exp == 0) return 0;
    if (exp == 0xFF) return (f >> 31) ? -32768 : 32767;
    int64_t mant = (int64_t)((f & 0x7FFFFF) | 0x800000);
    int shift = exp - 127 - 23;
    int64_t v = mant * 32767;
    if (shift >= 0) v = shift > 30 ? (int64_t)1 << 40 : v << shift;
    else v = shift < -60 ? 0 : v >> -shift;
    if (v > 32767) v = 32767;
    return (f >> 31) ? (int32_t)-v : (int32_t)v;
}

// Muestra (16 bits) del canal c de la trama i
int32_t sample(const Sound* s, uint64_t i, int c) {
    if (i >= s->frames) i = s->frames - 1;
    if (c >= s->channels) c = s->channels - 1;
    const uint8_t* p = s->data + i * s->align + (uint32_t)c * (s->bits / 8);
    if (s->is_float) return float_to_s16(le32(p));
    switch (s->bits) {
    case 8: return ((int32_t)p[0] - 128) << 8;
    case 16: return (int16_t)le16(p);
    case 24: return (int32_t)((uint32_t)p[1] << 16 | (uint32_t)p[2] << 24) >> 16;
    default: return (int32_t)le32(p) >> 16;
    }
}

uint32_t syncsafe(const uint8_t* p) { return (uint32_t)(p[0] & 0x7F) << 21 | (p[1] & 0x7F) << 14 | (p[2] & 0x7F) << 7 | (p[3] & 0x7F); }

bool mp3_refill(Mp3State* m) {
    if (m->next >= m->nframes) return false;
    Mp3Frame f{};
    int n;
    {
        FpuGuard g;
        uint32_t off = m->offs[m->next];
        n = mp3_frame(m->dec, m->data + off, (int)(m->size - off), m->tmp, f);
    }
    m->next++;
    int ch = f.channels;
    if (n <= 0 || n > MP3_MAX_SAMPLES) {   // trama dañada o sin datos previos: silencio
        n = (int)m->spf;
        ch = 1;
        memset(m->tmp, 0, (size_t)n * 2);
    }
    int16_t* dst;
    if (m->cur_n) {
        uint32_t last = m->cur_n - 1;
        m->cur[0] = m->cur[last * 2];
        m->cur[1] = m->cur[last * 2 + 1];
        m->cur_base += last;
        m->pos -= (uint64_t)last << 32;
        dst = m->cur + 2;
        m->cur_n = (uint32_t)n + 1;
    } else {
        dst = m->cur;
        m->cur_n = (uint32_t)n;
    }
    if (ch == 1) {
        for (int i = 0; i < n; i++) dst[i * 2] = dst[i * 2 + 1] = m->tmp[i];
    } else {
        memcpy(dst, m->tmp, (size_t)n * 4);
    }
    return true;
}

size_t mp3_read(Mp3State* m, int16_t* out, size_t frames) {
    size_t n = 0;
    for (; n < frames; n++) {
        uint64_t i = m->pos >> 32;
        while (i + 1 >= m->cur_n) {
            if (!mp3_refill(m)) return n;
            i = m->pos >> 32;
        }
        uint32_t frac = (uint32_t)(m->pos & 0xFFFFFFFF);
        for (int c = 0; c < 2; c++) {
            int32_t a = m->cur[i * 2 + c], b = m->cur[(i + 1) * 2 + c];
            out[n * 2 + c] = (int16_t)(a + (int32_t)(((int64_t)(b - a) * frac) >> 32));
        }
        m->pos += m->step;
    }
    return n;
}

void mp3_seek(Mp3State* m, uint64_t frame) {
    uint64_t total = (uint64_t)m->nframes * m->spf;
    if (frame > total) frame = total;
    uint32_t k = (uint32_t)(frame / m->spf);
    // se empieza dos tramas antes: el MP3 toma datos de las anteriores (depósito de bits)
    uint32_t start = k >= 2 ? k - 2 : 0;
    {
        FpuGuard g;
        mp3_reset(m->dec);
    }
    m->next = start;
    m->cur_n = 0;
    m->cur_base = (uint64_t)start * m->spf;
    m->pos = (frame - m->cur_base) << 32;
}

Mp3State* open_mp3(const uint8_t* d, uint32_t n, SoundInfo& info) {
    // etiquetas ID3: v2 al principio, v1 (128 bytes) al final
    uint32_t skip = 0;
    if (n >= 10 && !memcmp(d, "ID3", 3)) {
        skip = 10 + syncsafe(d + 6) + ((d[5] & 0x10) ? 10 : 0);
        if (skip >= n) return nullptr;
    }
    if (n - skip >= 128 && !memcmp(d + n - 128, "TAG", 3)) n -= 128;
    auto* m = (Mp3State*)mm::kcalloc(sizeof(Mp3State));
    if (!m) return nullptr;
    m->data = d + skip;
    m->size = n - skip;
    uint32_t cap = 0, kmin = 0xFFFF, kmax = 0;
    Mp3Frame f{}, first{};
    for (uint32_t pos = 0; pos < m->size;) {
        int s;
        {
            FpuGuard g;
            s = mp3_frame(m->dec, m->data + pos, (int)(m->size - pos), nullptr, f);
        }
        if (f.bytes <= 0) break;
        if (s > 0) {
            if (!m->nframes) { first = f; m->spf = (uint32_t)s; }
            if (m->nframes == cap) {
                uint32_t nc = cap ? cap * 2 : 1024;
                auto* no = (uint32_t*)mm::kmalloc(nc * 4ull);
                if (!no) break;
                if (m->offs) { memcpy(no, m->offs, cap * 4ull); mm::kfree(m->offs); }
                m->offs = no;
                cap = nc;
            }
            m->offs[m->nframes++] = pos + (uint32_t)f.offset;
            if ((uint32_t)f.kbps < kmin) kmin = (uint32_t)f.kbps;
            if ((uint32_t)f.kbps > kmax) kmax = (uint32_t)f.kbps;
        }
        pos += (uint32_t)f.bytes;
    }
    if (!m->nframes || !first.hz || !m->spf || m->spf > (uint32_t)MP3_MAX_SAMPLES) {
        if (m->offs) mm::kfree(m->offs);
        mm::kfree(m);
        return nullptr;
    }
    m->step = ((uint64_t)first.hz << 32) / 48000;
    mp3_seek(m, 0);
    info.rate = (uint32_t)first.hz;
    info.channels = (uint16_t)first.channels;
    info.bits = 16;
    info.frames = (uint64_t)m->nframes * m->spf;
    const char* name = first.layer == 3 ? "MP3" : first.layer == 2 ? "MP2" : "MP1";
    if (kmin == kmax) {
        ksnprintf(info.format, sizeof info.format, "%s %u kbps", name, kmax);
    } else {   // tasa variable: la media
        uint64_t avg = (uint64_t)m->size * 8 * (uint64_t)first.hz / (info.frames * 1000);
        ksnprintf(info.format, sizeof info.format, "%s VBR %u kbps", name, (unsigned)avg);
    }
    return m;
}

Sound* open(const uint8_t* d, uint32_t n, SoundInfo& info, const char*& err) {
    info = SoundInfo();
    if (n < 12 || memcmp(d, "RIFF", 4) || memcmp(d + 8, "WAVE", 4)) {
        Mp3State* m = open_mp3(d, n, info);
        if (!m) {
            info = SoundInfo();
            err = T("No es un archivo de sonido válido (WAV o MP3).");
            return nullptr;
        }
        auto* s = (Sound*)mm::kcalloc(sizeof(Sound));
        if (!s) {
            mm::kfree(m->offs);
            mm::kfree(m);
            err = T("No hay memoria suficiente.");
            return nullptr;
        }
        s->mp3 = m;
        s->rate = info.rate;
        s->frames = info.frames;
        return s;
    }
    const uint8_t* fmt = nullptr;
    const uint8_t* data = nullptr;
    uint32_t fmt_len = 0, data_len = 0;
    for (uint32_t pos = 12; pos + 8 <= n;) {
        uint32_t len = le32(d + pos + 4);
        if (!memcmp(d + pos, "fmt ", 4)) { fmt = d + pos + 8; fmt_len = len; }
        else if (!memcmp(d + pos, "data", 4)) {
            data = d + pos + 8;
            data_len = (uint64_t)pos + 8 + len > n ? n - pos - 8 : len;   // archivos cortados
            break;
        }
        pos += 8 + len + (len & 1);
    }
    if (!fmt || fmt_len < 16 || !data) { err = T("El archivo WAV está incompleto o dañado."); return nullptr; }
    uint16_t tag = le16(fmt);
    uint16_t channels = le16(fmt + 2), align = le16(fmt + 12), bits = le16(fmt + 14);
    uint32_t rate = le32(fmt + 4);
    if (tag == 0xFFFE && fmt_len >= 40) tag = le16(fmt + 24);   // WAVE_FORMAT_EXTENSIBLE
    bool is_float = tag == 3;
    if ((tag != 1 && tag != 3) || (is_float && bits != 32)) {
        err = T("Este tipo de WAV no está admitido (sólo PCM sin comprimir).");
        return nullptr;
    }
    if (!channels || !rate || rate > 384000 || (bits != 8 && bits != 16 && bits != 24 && bits != 32) ||
        align < channels * (bits / 8)) {
        err = T("El archivo WAV está incompleto o dañado.");
        return nullptr;
    }
    auto* s = (Sound*)mm::kcalloc(sizeof(Sound));
    if (!s) { err = T("No hay memoria suficiente."); return nullptr; }
    s->data = data;
    s->frames = data_len / align;
    s->rate = rate;
    s->channels = channels;
    s->bits = bits;
    s->align = align;
    s->is_float = is_float;
    s->step = ((uint64_t)rate << 32) / 48000;
    if (!s->frames) { mm::kfree(s); err = T("El archivo WAV no contiene sonido."); return nullptr; }
    info.rate = rate;
    info.channels = channels;
    info.bits = bits;
    info.frames = s->frames;
    ksnprintf(info.format, sizeof info.format, is_float ? "PCM float %u" : "PCM %u bits", bits);
    return s;
}

size_t read(Sound* s, int16_t* out, size_t frames) {
    if (s->mp3) return mp3_read(s->mp3, out, frames);
    size_t n = 0;
    for (; n < frames; n++) {
        uint64_t i = s->pos >> 32;
        if (i >= s->frames) break;
        uint32_t frac = (uint32_t)(s->pos & 0xFFFFFFFF);
        for (int c = 0; c < 2; c++) {
            int32_t a = sample(s, i, c), b = sample(s, i + 1, c);
            out[n * 2 + c] = (int16_t)(a + (int32_t)(((int64_t)(b - a) * frac) >> 32));
        }
        s->pos += s->step;
    }
    return n;
}

void seek(Sound* s, uint64_t frame) {
    if (s->mp3) return mp3_seek(s->mp3, frame);
    s->pos = (frame < s->frames ? frame : s->frames) << 32;
}
uint64_t tell(Sound* s) {
    if (s->mp3) {
        uint64_t p = s->mp3->cur_base + (s->mp3->pos >> 32);
        return p < s->frames ? p : s->frames;
    }
    return s->pos >> 32;
}
void close(Sound* s) {
    if (s->mp3) {
        mm::kfree(s->mp3->offs);
        mm::kfree(s->mp3);
    }
    mm::kfree(s);
}
bool can_open(const char* ext) {
    return ext && (!stricmp(ext, "WAV") || !stricmp(ext, "MP3") || !stricmp(ext, "MP2"));
}

const SoundApi api_v1 = {open, read, seek, tell, close, can_open};
const void* get_api(uint32_t v) { return v == SOUND_API ? &api_v1 : nullptr; }
}

BSL_DEFINE("sonido", "1.1", "Sonidos WAV y MP3 a 48 kHz estéreo", nullptr, get_api);

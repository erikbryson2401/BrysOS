// GIF 87a/89a: LZW, paletas global y local, entrelazado, transparencia y animación
// (cada fotograma se compone sobre el anterior según su método de eliminación).
#include "common.h"

using namespace img;

namespace {
constexpr int MAX_FRAMES = 500;

struct Lzw {
    // tabla de cadenas: prefijo + último byte; 'first' es el primer byte de la cadena
    uint16_t prefix[4096];
    uint8_t suffix[4096];
    uint8_t first[4096];
    uint16_t length[4096];
};

// Decodifica los subbloques de datos de una imagen en 'out' (w*h índices)
bool lzw_decode(const uint8_t*& p, const uint8_t* end, int min_code, uint8_t* out, size_t total, Lzw& t) {
    if (min_code < 2 || min_code > 8) return false;
    int clear = 1 << min_code, eoi = clear + 1;
    int size = min_code + 1, next = eoi + 1, prev = -1;
    for (int i = 0; i < clear; i++) {
        t.prefix[i] = 0xFFFF;
        t.suffix[i] = (uint8_t)i;
        t.first[i] = (uint8_t)i;
        t.length[i] = 1;
    }
    uint32_t buf = 0;
    int bits = 0;
    size_t written = 0;
    int block = 0;
    bool done = false;
    while (!done) {
        // lee bytes de los subbloques
        while (bits < size) {
            if (!block) {
                if (p >= end) { done = true; break; }
                block = *p++;
                if (!block) { done = true; break; }
            }
            if (p >= end) { done = true; break; }
            buf |= (uint32_t)*p++ << bits;
            bits += 8;
            block--;
        }
        if (done) break;
        int code = (int)(buf & ((1u << size) - 1));
        buf >>= size;
        bits -= size;
        if (code == clear) {
            size = min_code + 1;
            next = eoi + 1;
            prev = -1;
            continue;
        }
        if (code == eoi) break;
        int c = code;
        uint8_t first_byte;
        if (code < next) {
            first_byte = t.first[code];
        } else if (code == next && prev >= 0) {
            first_byte = t.first[prev];
        } else {
            break;   // código no válido: se deja lo decodificado
        }
        if (prev >= 0 && next < 4096) {
            t.prefix[next] = (uint16_t)prev;
            t.suffix[next] = first_byte;
            t.first[next] = t.first[prev];
            t.length[next] = (uint16_t)(t.length[prev] + 1);
            next++;
            if (next == (1 << size) && size < 12) size++;
        }
        // escribe la cadena (al revés desde su final)
        int len = t.length[c];
        if (written + (size_t)len > total) len = (int)(total - written);
        size_t pos = written + (size_t)t.length[c] - 1;
        int k = c;
        for (int i = t.length[c] - 1; i >= 0 && k != 0xFFFF; i--, pos--) {
            if (pos < total) out[pos] = t.suffix[k];
            k = t.prefix[k];
        }
        written += (size_t)len;
        prev = code;
        if (written >= total) break;
    }
    // salta el resto de subbloques
    while (block && p < end) { p++; block--; }
    while (p < end) {
        int b = *p++;
        if (!b) break;
        p += b;
    }
    return true;
}
}

bool img::decode_gif(const uint8_t* d, uint32_t n, Image& out, const char*& err) {
    if (n < 13 || (memcmp(d, "GIF87a", 6) && memcmp(d, "GIF89a", 6))) { err = T("No es un archivo GIF válido."); return false; }
    int W = le16(d + 6), H = le16(d + 8);
    uint8_t flags = d[10];
    int bg = d[11];
    if (W <= 0 || H <= 0 || W > MAX_SIDE || H > MAX_SIDE || (uint64_t)W * H > MAX_PIXELS) {
        err = T("La imagen es demasiado grande.");
        return false;
    }
    const uint8_t* p = d + 13;
    const uint8_t* end = d + n;
    uint32_t gpal[256] = {};
    if (flags & 0x80) {
        int sz = 2 << (flags & 7);
        if (p + sz * 3 > end) { err = T("El archivo GIF está dañado."); return false; }
        for (int i = 0; i < sz; i++) gpal[i] = 0xFF000000u | ((uint32_t)p[i * 3] << 16) | ((uint32_t)p[i * 3 + 1] << 8) | p[i * 3 + 2];
        p += sz * 3;
    }
    (void)bg;   // como los navegadores: el fondo se deja transparente
    auto* lzw = (Lzw*)img_alloc(sizeof(Lzw));
    auto* canvas = (uint32_t*)img_calloc((size_t)W * H * 4);
    auto* prev_canvas = (uint32_t*)img_alloc((size_t)W * H * 4);
    auto* frames = (ImageFrame*)img_calloc(sizeof(ImageFrame) * MAX_FRAMES);
    uint8_t* idx = nullptr;
    int count = 0;
    bool ok = lzw && canvas && prev_canvas && frames;
    int delay = 0, transparent = -1, disposal = 0;
    // las animaciones largas se cortan a 256 MB de fotogramas
    size_t max_frames = minv<size_t>(MAX_FRAMES, maxv<size_t>(1, (256u << 20) / ((size_t)W * H * 4)));
    while (ok && p < end && (size_t)count < max_frames) {
        uint8_t b = *p++;
        if (b == 0x3B) break;   // final
        if (b == 0x21) {        // extensión
            if (p >= end) break;
            uint8_t label = *p++;
            if (label == 0xF9 && p + 6 <= end && p[0] >= 4) {   // control de gráficos
                disposal = (p[1] >> 2) & 7;
                delay = le16(p + 2) * 10;
                transparent = (p[1] & 1) ? p[4] : -1;
            }
            while (p < end) {
                int sz = *p++;
                if (!sz) break;
                p += sz;
            }
            continue;
        }
        if (b != 0x2C) break;   // bloque desconocido
        if (p + 9 > end) break;
        int fx = le16(p), fy = le16(p + 2), fw = le16(p + 4), fh = le16(p + 6);
        uint8_t ff = p[8];
        p += 9;
        uint32_t lpal[256];
        const uint32_t* pal = gpal;
        if (ff & 0x80) {
            int sz = 2 << (ff & 7);
            if (p + sz * 3 > end) break;
            for (int i = 0; i < sz; i++) lpal[i] = 0xFF000000u | ((uint32_t)p[i * 3] << 16) | ((uint32_t)p[i * 3 + 1] << 8) | p[i * 3 + 2];
            for (int i = sz; i < 256; i++) lpal[i] = 0xFF000000u;
            p += sz * 3;
            pal = lpal;
        }
        if (p >= end || fw <= 0 || fh <= 0) break;
        int min_code = *p++;
        img_free(idx);
        idx = (uint8_t*)img_calloc((size_t)fw * fh);
        if (!idx) { ok = false; break; }
        lzw_decode(p, end, min_code, idx, (size_t)fw * fh, *lzw);
        if (disposal == 3) memcpy(prev_canvas, canvas, (size_t)W * H * 4);
        // filas entrelazadas: pasadas de 8, 8 (desde 4), 4 (desde 2) y 2 (desde 1)
        bool inter = ff & 0x40;
        int row = 0;
        static const int start[4] = {0, 4, 2, 1}, step[4] = {8, 8, 4, 2};
        int pass = 0, iy = 0;
        for (int sy = 0; sy < fh; sy++) {
            if (inter) {
                while (pass < 4 && iy >= fh) { pass++; iy = pass < 4 ? start[pass] : 0; }
                row = iy;
                iy += step[pass < 4 ? pass : 3];
            } else {
                row = sy;
            }
            int y = fy + row;
            if (y < 0 || y >= H) continue;
            for (int x = 0; x < fw; x++) {
                int cx = fx + x;
                if (cx < 0 || cx >= W) continue;
                int c = idx[(size_t)sy * fw + x];
                if (c == transparent) continue;
                canvas[(size_t)y * W + cx] = pal[c];
            }
        }
        // copia del fotograma compuesto
        frames[count].px = (uint32_t*)img_alloc((size_t)W * H * 4);
        if (!frames[count].px) { ok = count > 0; break; }
        memcpy(frames[count].px, canvas, (size_t)W * H * 4);
        frames[count].delay_ms = (uint32_t)(delay >= 20 ? delay : 100);   // como los navegadores
        count++;
        // eliminación antes del siguiente fotograma
        if (disposal == 2) {
            for (int y = maxv(fy, 0); y < minv(fy + fh, H); y++)
                for (int x = maxv(fx, 0); x < minv(fx + fw, W); x++) canvas[(size_t)y * W + x] = 0;
        } else if (disposal == 3) {
            memcpy(canvas, prev_canvas, (size_t)W * H * 4);
        }
        delay = 0;
        transparent = -1;
        disposal = 0;
    }
    img_free(idx);
    img_free(lzw);
    img_free(canvas);
    img_free(prev_canvas);
    if (!ok || !count) {
        for (int i = 0; i < count; i++) img_free(frames[i].px);
        img_free(frames);
        err = ok ? T("El archivo GIF no contiene ninguna imagen.") : T("No hay memoria suficiente para abrir la imagen.");
        return false;
    }
    out.w = W;
    out.h = H;
    out.frames = frames;
    out.frame_count = count;
    memcpy(out.format, "GIF", 4);
    return true;
}

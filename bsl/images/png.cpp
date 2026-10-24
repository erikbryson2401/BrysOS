// PNG: todos los tipos de color (gris, RGB, paleta, con y sin alfa), profundidades de 1 a
// 16 bits, transparencia tRNS y entrelazado Adam7.
#include "common.h"

using namespace img;

namespace {
struct Png {
    int w = 0, h = 0, depth = 0, color = 0, interlace = 0;
    uint32_t palette[256];
    int palette_size = 0;
    bool has_trns = false;
    uint16_t trns_gray = 0, trns_r = 0, trns_g = 0, trns_b = 0;
};

int channels(int color) {
    switch (color) {
    case 0: return 1;
    case 2: return 3;
    case 3: return 1;
    case 4: return 2;
    case 6: return 4;
    }
    return 0;
}

uint8_t paeth(int a, int b, int c) {
    int p = a + b - c;
    int pa = p > a ? p - a : a - p, pb = p > b ? p - b : b - p, pc = p > c ? p - c : c - p;
    if (pa <= pb && pa <= pc) return (uint8_t)a;
    if (pb <= pc) return (uint8_t)b;
    return (uint8_t)c;
}

// Quita los filtros de una imagen (o de una pasada de Adam7) de w x h
bool unfilter(uint8_t* data, int w, int h, int bpp_bits, size_t& consumed) {
    size_t stride = ((size_t)w * bpp_bits + 7) / 8;
    int bpp = (bpp_bits + 7) / 8;   // bytes por píxel para el filtro (mínimo 1)
    uint8_t* prev = nullptr;
    uint8_t* p = data;
    for (int y = 0; y < h; y++) {
        uint8_t f = *p;
        uint8_t* row = p + 1;
        switch (f) {
        case 0: break;
        case 1: for (size_t i = (size_t)bpp; i < stride; i++) row[i] = (uint8_t)(row[i] + row[i - bpp]); break;
        case 2: if (prev) for (size_t i = 0; i < stride; i++) row[i] = (uint8_t)(row[i] + prev[i]); break;
        case 3:
            for (size_t i = 0; i < stride; i++) {
                int a = i >= (size_t)bpp ? row[i - bpp] : 0, b = prev ? prev[i] : 0;
                row[i] = (uint8_t)(row[i] + ((a + b) >> 1));
            }
            break;
        case 4:
            for (size_t i = 0; i < stride; i++) {
                int a = i >= (size_t)bpp ? row[i - bpp] : 0, b = prev ? prev[i] : 0;
                int c = (prev && i >= (size_t)bpp) ? prev[i - bpp] : 0;
                row[i] = (uint8_t)(row[i] + paeth(a, b, c));
            }
            break;
        default: return false;
        }
        prev = row;
        p += stride + 1;
    }
    consumed = (size_t)(p - data);
    return true;
}

// Muestra (canal 'c') del píxel x de una fila ya sin filtro, normalizada a 16 bits
uint16_t sample(const uint8_t* row, int x, int c, int ch, int depth) {
    if (depth == 16) {
        const uint8_t* s = row + ((size_t)x * ch + c) * 2;
        return (uint16_t)((s[0] << 8) | s[1]);
    }
    if (depth == 8) return row[(size_t)x * ch + c];
    // 1, 2 o 4 bits (sólo gris o paleta: un canal)
    int per = 8 / depth;
    uint8_t byte = row[x / per];
    int shift = 8 - depth * (x % per + 1);
    return (uint16_t)((byte >> shift) & ((1 << depth) - 1));
}

uint32_t to_argb(const Png& p, const uint8_t* row, int x) {
    int ch = channels(p.color);
    auto s8 = [&](int c) -> uint32_t {
        uint16_t v = sample(row, x, c, ch, p.depth);
        if (p.depth == 16) return v >> 8;
        if (p.depth < 8) return (uint32_t)(v * 255 / ((1 << p.depth) - 1));
        return v;
    };
    switch (p.color) {
    case 0: {
        uint32_t g = s8(0);
        bool clear = p.has_trns && sample(row, x, 0, 1, p.depth) == p.trns_gray;
        return (clear ? 0u : 0xFF000000u) | (g << 16) | (g << 8) | g;
    }
    case 2: {
        bool clear = p.has_trns && sample(row, x, 0, 3, p.depth) == p.trns_r && sample(row, x, 1, 3, p.depth) == p.trns_g &&
                     sample(row, x, 2, 3, p.depth) == p.trns_b;
        return (clear ? 0u : 0xFF000000u) | (s8(0) << 16) | (s8(1) << 8) | s8(2);
    }
    case 3: {
        uint16_t i = sample(row, x, 0, 1, p.depth);
        return i < p.palette_size ? p.palette[i] : 0xFF000000u;
    }
    case 4: {
        uint32_t g = s8(0);
        return (s8(1) << 24) | (g << 16) | (g << 8) | g;
    }
    default:
        return (s8(3) << 24) | (s8(0) << 16) | (s8(1) << 8) | s8(2);
    }
}
}

bool img::decode_png(const uint8_t* d, uint32_t n, Image& out, const char*& err) {
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 13, 10, 26, 10};
    if (n < 8 || memcmp(d, sig, 8)) { err = T("No es un archivo PNG válido."); return false; }
    Png p;
    for (int i = 0; i < 256; i++) p.palette[i] = 0xFF000000u;
    // 1. Fragmentos: cabecera, paleta, transparencia y tamaño total de los datos
    size_t idat_total = 0;
    bool have_ihdr = false;
    for (uint32_t pos = 8; pos + 12 <= n;) {
        uint32_t len = be32(d + pos);
        const uint8_t* type = d + pos + 4;
        const uint8_t* body = d + pos + 8;
        if ((uint64_t)pos + 12 + len > n) break;
        if (!memcmp(type, "IHDR", 4) && len >= 13) {
            p.w = (int)be32(body);
            p.h = (int)be32(body + 4);
            p.depth = body[8];
            p.color = body[9];
            p.interlace = body[12];
            have_ihdr = true;
        } else if (!memcmp(type, "PLTE", 4)) {
            p.palette_size = (int)minv<uint32_t>(len / 3, 256);
            for (int i = 0; i < p.palette_size; i++)
                p.palette[i] = 0xFF000000u | ((uint32_t)body[i * 3] << 16) | ((uint32_t)body[i * 3 + 1] << 8) | body[i * 3 + 2];
        } else if (!memcmp(type, "tRNS", 4)) {
            p.has_trns = true;
            if (p.color == 3) {
                for (uint32_t i = 0; i < len && i < 256; i++) p.palette[i] = (p.palette[i] & 0xFFFFFF) | ((uint32_t)body[i] << 24);
            } else if (p.color == 0 && len >= 2) {
                p.trns_gray = be16(body);
            } else if (p.color == 2 && len >= 6) {
                p.trns_r = be16(body);
                p.trns_g = be16(body + 2);
                p.trns_b = be16(body + 4);
            }
        } else if (!memcmp(type, "IDAT", 4)) {
            idat_total += len;
        } else if (!memcmp(type, "IEND", 4)) {
            break;
        }
        pos += 12 + len;
    }
    int ch = channels(p.color);
    bool depth_ok = p.depth == 8 || p.depth == 16 || ((p.color == 0 || p.color == 3) && (p.depth == 1 || p.depth == 2 || p.depth == 4));
    if (!have_ihdr || !ch || !depth_ok || p.interlace > 1 || !idat_total) { err = T("Este tipo de PNG no está admitido."); return false; }
    if (p.w <= 0 || p.h <= 0 || p.w > MAX_SIDE || p.h > MAX_SIDE || (uint64_t)p.w * p.h > MAX_PIXELS) {
        err = T("La imagen es demasiado grande.");
        return false;
    }
    // 2. Datos comprimidos juntos y descomprimidos
    auto* z = (uint8_t*)img_alloc(idat_total);
    if (!z) { err = T("No hay memoria suficiente para abrir la imagen."); return false; }
    size_t zp = 0;
    for (uint32_t pos = 8; pos + 12 <= n;) {
        uint32_t len = be32(d + pos);
        if ((uint64_t)pos + 12 + len > n) break;
        if (!memcmp(d + pos + 4, "IDAT", 4)) {
            memcpy(z + zp, d + pos + 8, len);
            zp += len;
        }
        pos += 12 + len;
    }
    int bits = ch * p.depth;
    // tamaño sin comprimir (con el byte de filtro de cada fila y de cada pasada)
    static const int ax0[7] = {0, 4, 0, 2, 0, 1, 0}, ay0[7] = {0, 0, 4, 0, 2, 0, 1};
    static const int adx[7] = {8, 8, 4, 4, 2, 2, 1}, ady[7] = {8, 8, 8, 4, 4, 2, 2};
    size_t raw_len = 0;
    if (!p.interlace) raw_len = (size_t)p.h * (1 + ((size_t)p.w * bits + 7) / 8);
    else
        for (int k = 0; k < 7; k++) {
            int pw = (p.w - ax0[k] + adx[k] - 1) / adx[k], ph = (p.h - ay0[k] + ady[k] - 1) / ady[k];
            if (pw > 0 && ph > 0) raw_len += (size_t)ph * (1 + ((size_t)pw * bits + 7) / 8);
        }
    auto* raw = (uint8_t*)img_alloc(raw_len);
    size_t got = 0;
    bool ok = raw && zlib_inflate(z, idat_total, raw, raw_len, got) && got == raw_len;
    img_free(z);
    if (!ok) {
        img_free(raw);
        err = T("El archivo PNG está dañado (datos comprimidos no válidos).");
        return false;
    }
    if (!alloc_frames(out, p.w, p.h, 1)) {
        img_free(raw);
        err = T("No hay memoria suficiente para abrir la imagen.");
        return false;
    }
    uint32_t* px = out.frames[0].px;
    // 3. Filtros y conversión a ARGB
    if (!p.interlace) {
        size_t used;
        ok = unfilter(raw, p.w, p.h, bits, used);
        size_t stride = 1 + ((size_t)p.w * bits + 7) / 8;
        for (int y = 0; ok && y < p.h; y++)
            for (int x = 0; x < p.w; x++) px[(size_t)y * p.w + x] = to_argb(p, raw + (size_t)y * stride + 1, x);
    } else {
        uint8_t* pass = raw;
        for (int k = 0; ok && k < 7; k++) {
            int pw = (p.w - ax0[k] + adx[k] - 1) / adx[k], ph = (p.h - ay0[k] + ady[k] - 1) / ady[k];
            if (pw <= 0 || ph <= 0) continue;
            size_t used;
            ok = unfilter(pass, pw, ph, bits, used);
            size_t stride = 1 + ((size_t)pw * bits + 7) / 8;
            for (int y = 0; ok && y < ph; y++)
                for (int x = 0; x < pw; x++)
                    px[(size_t)(ay0[k] + y * ady[k]) * p.w + ax0[k] + x * adx[k]] = to_argb(p, pass + (size_t)y * stride + 1, x);
            pass += used;
        }
    }
    img_free(raw);
    if (!ok) {
        free_image(out);
        err = T("El archivo PNG está dañado (filtro desconocido).");
        return false;
    }
    memcpy(out.format, "PNG", 4);
    return true;
}

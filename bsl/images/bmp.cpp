// BMP: cabeceras de OS/2 y Windows (v3, v4, v5), 1/4/8/16/24/32 bits, campos de bits,
// compresión RLE4/RLE8 y filas de arriba abajo o de abajo arriba.
#include "common.h"

using namespace img;

namespace {
int mask_shift(uint32_t m) {
    if (!m) return 0;
    int s = 0;
    while (!(m & 1)) { m >>= 1; s++; }
    return s;
}
int mask_bits(uint32_t m) {
    int b = 0;
    m >>= mask_shift(m);
    while (m & 1) { m >>= 1; b++; }
    return b;
}
uint32_t scale(uint32_t v, int bits) {
    if (bits <= 0) return 0;
    if (bits >= 8) return v >> (bits - 8);
    return (v * 255) / ((1u << bits) - 1);
}
}

bool img::decode_bmp(const uint8_t* d, uint32_t n, Image& out, const char*& err) {
    const char* bad = T("No es un archivo BMP válido o está dañado.");
    if (n < 26 || d[0] != 'B' || d[1] != 'M') { err = bad; return false; }
    uint32_t data_off = le32(d + 10), hsize = le32(d + 14);
    int w, h, bpp;
    uint32_t comp = 0, colors = 0;
    uint32_t rmask = 0, gmask = 0, bmask = 0, amask = 0;
    if (hsize == 12) {
        w = le16(d + 18);
        h = (int16_t)le16(d + 20);
        bpp = le16(d + 24);
    } else if (hsize >= 40 && 14 + hsize <= n) {
        w = (int)le32(d + 18);
        h = (int)le32(d + 22);
        bpp = le16(d + 28);
        comp = le32(d + 30);
        colors = le32(d + 46);
        if (comp == 3 || comp == 6) {
            // máscaras tras la cabecera v3 o dentro de las v4/v5
            const uint8_t* m = d + 14 + 40;
            if (hsize >= 52 || m + 12 <= d + n) {
                rmask = le32(m);
                gmask = le32(m + 4);
                bmask = le32(m + 8);
            }
            if (hsize >= 56 || comp == 6) amask = le32(m + 12);
        } else if (hsize >= 56) {
            amask = le32(d + 14 + 52);
        }
    } else {
        err = bad;
        return false;
    }
    bool top_down = h < 0;
    if (top_down) h = -h;
    if (w <= 0 || h <= 0 || w > MAX_SIDE || h > MAX_SIDE || (uint64_t)w * h > MAX_PIXELS) { err = T("La imagen es demasiado grande."); return false; }
    if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32) { err = T("Profundidad de color no admitida."); return false; }
    if (comp != 0 && comp != 1 && comp != 2 && comp != 3 && comp != 6) { err = T("Los mapas de bits comprimidos no están admitidos."); return false; }
    // paleta
    uint32_t pal[256];
    for (auto& c : pal) c = 0xFF000000u;
    if (bpp <= 8) {
        int entry = hsize == 12 ? 3 : 4;
        uint32_t count = colors ? colors : (1u << bpp);
        const uint8_t* pp = d + 14 + hsize;
        for (uint32_t i = 0; i < count && i < 256 && pp + entry <= d + n; i++, pp += entry)
            pal[i] = 0xFF000000u | ((uint32_t)pp[2] << 16) | ((uint32_t)pp[1] << 8) | pp[0];
    }
    if (bpp == 16 && comp != 3 && comp != 6) { rmask = 0x7C00; gmask = 0x03E0; bmask = 0x001F; }
    if (bpp == 32 && comp != 3 && comp != 6) { rmask = 0xFF0000; gmask = 0xFF00; bmask = 0xFF; }
    if (data_off >= n) { err = bad; return false; }
    if (!alloc_frames(out, w, h, 1)) { err = T("No hay memoria suficiente para abrir la imagen."); return false; }
    uint32_t* px = out.frames[0].px;
    const uint8_t* src = d + data_off;
    const uint8_t* end = d + n;
    auto put = [&](int x, int row, uint32_t c) {
        int y = top_down ? row : h - 1 - row;
        if (x >= 0 && x < w && y >= 0 && y < h) px[(size_t)y * w + x] = c;
    };
    if (comp == 1 || comp == 2) {
        // RLE8 / RLE4
        for (size_t i = 0; i < (size_t)w * h; i++) px[i] = 0xFF000000u | pal[0];
        int x = 0, row = 0;
        const uint8_t* p = src;
        while (p + 1 < end && row < h) {
            int a = p[0], b = p[1];
            p += 2;
            if (a) {
                for (int i = 0; i < a; i++)
                    put(x++, row, pal[comp == 1 ? b : ((i & 1) ? (b & 15) : (b >> 4))]);
            } else if (b == 0) { x = 0; row++; }
            else if (b == 1) break;
            else if (b == 2) {
                if (p + 1 >= end) break;
                x += p[0];
                row += p[1];
                p += 2;
            } else {
                for (int i = 0; i < b; i++) {
                    int v;
                    if (comp == 1) v = p < end ? p[i] : 0;
                    else v = (p + i / 2 < end) ? ((i & 1) ? (p[i / 2] & 15) : (p[i / 2] >> 4)) : 0;
                    put(x++, row, pal[v]);
                }
                int used = comp == 1 ? b : (b + 1) / 2;
                p += (used + 1) & ~1;
            }
        }
    } else {
        size_t stride = (((size_t)w * bpp + 31) / 32) * 4;
        int rs = mask_shift(rmask), gs = mask_shift(gmask), bs = mask_shift(bmask), as = mask_shift(amask);
        int rb = mask_bits(rmask), gb = mask_bits(gmask), bb = mask_bits(bmask), ab = mask_bits(amask);
        for (int row = 0; row < h; row++) {
            const uint8_t* line = src + (size_t)row * stride;
            if (line + stride > end) break;
            for (int x = 0; x < w; x++) {
                uint32_t c;
                if (bpp <= 8) {
                    int per = 8 / bpp;
                    int v = (line[x / per] >> (8 - bpp * (x % per + 1))) & ((1 << bpp) - 1);
                    c = pal[v];
                } else if (bpp == 24) {
                    const uint8_t* q = line + x * 3;
                    c = 0xFF000000u | ((uint32_t)q[2] << 16) | ((uint32_t)q[1] << 8) | q[0];
                } else {
                    uint32_t v = bpp == 16 ? le16(line + x * 2) : le32(line + x * 4);
                    uint32_t r = scale((v & rmask) >> rs, rb), g = scale((v & gmask) >> gs, gb), b = scale((v & bmask) >> bs, bb);
                    uint32_t a = amask ? scale((v & amask) >> as, ab) : 255;
                    c = (a << 24) | (r << 16) | (g << 8) | b;
                }
                put(x, row, c);
            }
        }
    }
    memcpy(out.format, "BMP", 4);
    return true;
}

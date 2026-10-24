#include "apps/bmp.h"
#include "lib/i18n.h"

static uint32_t u32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t u16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

bool bmp_decode(const uint8_t* d, uint32_t size, Surface& out, String& err) {
    if (size < 54 || d[0] != 'B' || d[1] != 'M') { err = T("No es un archivo de mapa de bits válido."); return false; }
    uint32_t off = u32(d + 10);
    uint32_t hsize = u32(d + 14);
    if (hsize < 40) { err = T("Formato de mapa de bits no admitido (cabecera antigua)."); return false; }
    int32_t w = (int32_t)u32(d + 18);
    int32_t h = (int32_t)u32(d + 22);
    uint16_t bpp = u16(d + 28);
    uint32_t comp = u32(d + 30);
    uint32_t ncolors = u32(d + 46);
    bool top_down = h < 0;
    if (h < 0) h = -h;
    if (w <= 0 || h <= 0 || w > 8192 || h > 8192) { err = T("Dimensiones de imagen no válidas."); return false; }
    if (comp != 0 && !(comp == 3 && (bpp == 32 || bpp == 16))) { err = T("Los mapas de bits comprimidos no están admitidos."); return false; }
    if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 24 && bpp != 32) { err = T("Profundidad de color no admitida."); return false; }
    uint32_t pal_off = 14 + hsize;
    if (bpp <= 8 && !ncolors) ncolors = 1u << bpp;
    uint32_t stride = ((uint32_t)w * bpp + 31) / 32 * 4;
    if (off + stride * (uint32_t)h > size) { err = T("El archivo está incompleto o dañado."); return false; }
    out.resize(w, h);
    for (int y = 0; y < h; y++) {
        const uint8_t* row = d + off + (uint32_t)(top_down ? y : h - 1 - y) * stride;
        for (int x = 0; x < w; x++) {
            uint32_t c = 0;
            if (bpp == 24) c = row[x * 3] | (row[x * 3 + 1] << 8) | (row[x * 3 + 2] << 16);
            else if (bpp == 32) c = u32(row + x * 4) & 0xFFFFFF;
            else {
                uint32_t idx;
                if (bpp == 8) idx = row[x];
                else if (bpp == 4) idx = (row[x / 2] >> ((x & 1) ? 0 : 4)) & 15;
                else idx = (row[x / 8] >> (7 - (x & 7))) & 1;
                if (idx < ncolors && pal_off + idx * 4 + 3 < size) {
                    const uint8_t* p = d + pal_off + idx * 4;
                    c = p[0] | (p[1] << 8) | (p[2] << 16);
                }
            }
            out.px[y * out.stride + x] = c;
        }
    }
    return true;
}

Vec<uint8_t> bmp_encode(const Surface& s) {
    uint32_t stride = ((uint32_t)s.w * 3 + 3) & ~3u;
    uint32_t img = stride * (uint32_t)s.h;
    Vec<uint8_t> out;
    out.resize(54 + img);
    uint8_t* d = out.data();
    memset(d, 0, 54 + img);
    auto w32 = [&](int o, uint32_t v) { d[o] = v & 255; d[o + 1] = (v >> 8) & 255; d[o + 2] = (v >> 16) & 255; d[o + 3] = v >> 24; };
    d[0] = 'B'; d[1] = 'M';
    w32(2, 54 + img);
    w32(10, 54);
    w32(14, 40);
    w32(18, (uint32_t)s.w);
    w32(22, (uint32_t)s.h);
    d[26] = 1;
    d[28] = 24;
    w32(34, img);
    w32(38, 2835);
    w32(42, 2835);
    for (int y = 0; y < s.h; y++) {
        uint8_t* row = d + 54 + (uint32_t)(s.h - 1 - y) * stride;
        for (int x = 0; x < s.w; x++) {
            uint32_t c = s.px[y * s.stride + x];
            row[x * 3] = c & 255;
            row[x * 3 + 1] = (c >> 8) & 255;
            row[x * 3 + 2] = (c >> 16) & 255;
        }
    }
    return out;
}

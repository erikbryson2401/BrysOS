// JPEG: secuencial (baseline y extendido de 8 bits) y progresivo, códigos de Huffman,
// submuestreo de color (con el remuestreo suave de libjpeg para 4:2:0 y 4:2:2), marcadores
// de reinicio, gris, YCbCr, CMYK/YCCK de Adobe y orientación EXIF de las fotos.
#include "common.h"

using namespace img;

namespace {
const uint8_t ZZ[64 + 16] = {
    0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,  12, 19, 26, 33, 40, 48,
    41, 34, 27, 20, 13, 6,  7,  14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23,
    30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
    // margen: índices fuera de rango en datos dañados caen en el último coeficiente
    63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63};

struct Huff {
    bool present = false;
    uint8_t look_bits[512];
    uint8_t look_sym[512];
    int32_t maxcode[18];
    int32_t mincode[17];
    int valptr[17];
    uint8_t vals[256];
};

bool build_huff(Huff& h, const uint8_t* counts, const uint8_t* vals, int nvals) {
    memcpy(h.vals, vals, (size_t)nvals);
    for (int i = 0; i < 512; i++) h.look_bits[i] = 0;
    int code = 0, k = 0;
    for (int len = 1; len <= 16; len++) {
        h.valptr[len] = k;
        h.mincode[len] = code;
        int c = counts[len - 1];
        for (int i = 0; i < c; i++, k++, code++) {
            if (len <= 9) {
                int base = code << (9 - len);
                for (int f = 0; f < (1 << (9 - len)); f++) {
                    h.look_bits[base + f] = (uint8_t)len;
                    h.look_sym[base + f] = vals[k];
                }
            }
        }
        h.maxcode[len] = c ? code - 1 : -1;
        if (code > (1 << len)) return false;
        code <<= 1;
    }
    h.maxcode[17] = 0x7FFFFFFF;
    h.present = true;
    return true;
}

struct Reader {
    const uint8_t* p;
    const uint8_t* end;
    uint32_t buf = 0;
    int bits = 0;
    bool marker = false;   // se ha llegado a un marcador: a partir de aquí, ceros

    void fill() {
        while (bits <= 24) {
            uint32_t b = 0;
            if (!marker && p < end) {
                b = *p;
                if (b == 0xFF) {
                    uint8_t nb = p + 1 < end ? p[1] : 0xD9;
                    if (nb == 0) p += 2;
                    else { marker = true; b = 0; }
                } else {
                    p++;
                }
            }
            buf |= b << (24 - bits);
            bits += 8;
        }
    }
    uint32_t get(int n) {
        if (!n) return 0;
        if (bits < n) fill();
        uint32_t v = buf >> (32 - n);
        buf <<= n;
        bits -= n;
        return v;
    }
    int bit() { return (int)get(1); }
    void reset() { buf = 0; bits = 0; marker = false; }
};

int decode(Reader& r, const Huff& h) {
    if (r.bits < 16) r.fill();
    uint32_t look = r.buf >> 23;
    int nb = h.look_bits[look];
    if (nb) {
        r.buf <<= nb;
        r.bits -= nb;
        return h.look_sym[look];
    }
    for (int len = 10; len <= 16; len++) {
        int32_t code = (int32_t)(r.buf >> (32 - len));
        if (code <= h.maxcode[len]) {
            r.buf <<= len;
            r.bits -= len;
            int idx = h.valptr[len] + code - h.mincode[len];
            return idx >= 0 && idx < 256 ? h.vals[idx] : 0;
        }
    }
    r.buf <<= 16;   // código no válido: datos dañados
    r.bits -= 16;
    return 0;
}

int extend(uint32_t v, int s) {
    if (!s) return 0;
    return v < (1u << (s - 1)) ? (int)v - (1 << s) + 1 : (int)v;
}

struct Comp {
    int id, h, v, tq;
    int bw, bh;            // bloques de 8x8 (rellenando hasta completar las MCU)
    int cw, ch;            // tamaño real en píxeles de la componente
    int16_t* coef = nullptr;
    uint8_t* plane = nullptr;
    int dc_pred = 0;
    int td = 0, ta = 0;    // tablas de Huffman del barrido en curso
};

struct Jpeg {
    int w = 0, h = 0, ncomp = 0;
    bool progressive = false;
    Comp comp[4];
    int hmax = 1, vmax = 1, mcux = 0, mcuy = 0;
    uint16_t q[4][64];
    Huff dc[4], ac[4];
    int restart = 0;
    int eobrun = 0;
    int adobe = -1;        // transformación de color del marcador APP14 de Adobe
    int orientation = 1;   // EXIF
    bool jfif = false;
};

// --- Barridos
void decode_block_baseline(Jpeg& j, Reader& r, Comp& c, int16_t* blk) {
    int t = decode(r, j.dc[c.td]);
    c.dc_pred += extend(r.get(t), t);
    blk[0] = (int16_t)c.dc_pred;
    for (int k = 1; k < 64;) {
        int rs = decode(r, j.ac[c.ta]);
        int run = rs >> 4, s = rs & 15;
        if (!s) {
            if (run != 15) break;
            k += 16;
            continue;
        }
        k += run;
        if (k > 63) break;
        blk[ZZ[k]] = (int16_t)extend(r.get(s), s);
        k++;
    }
}

void decode_dc_first(Jpeg& j, Reader& r, Comp& c, int16_t* blk, int al) {
    int t = decode(r, j.dc[c.td]);
    c.dc_pred += extend(r.get(t), t);
    blk[0] = (int16_t)(c.dc_pred * (1 << al));
}

void decode_dc_refine(Reader& r, int16_t* blk, int al) {
    if (r.bit()) blk[0] = (int16_t)(blk[0] | (1 << al));
}

void decode_ac_first(Jpeg& j, Reader& r, Comp& c, int16_t* blk, int ss, int se, int al) {
    if (j.eobrun) { j.eobrun--; return; }
    for (int k = ss; k <= se;) {
        int rs = decode(r, j.ac[c.ta]);
        int run = rs >> 4, s = rs & 15;
        if (!s) {
            if (run < 15) {
                j.eobrun = (1 << run) - 1;
                if (run) j.eobrun += (int)r.get(run);
                break;
            }
            k += 16;
            continue;
        }
        k += run;
        if (k > 63) break;
        blk[ZZ[k]] = (int16_t)(extend(r.get(s), s) * (1 << al));
        k++;
    }
}

void decode_ac_refine(Jpeg& j, Reader& r, Comp& c, int16_t* blk, int ss, int se, int al) {
    int p1 = 1 << al, m1 = -1 * (1 << al);
    int k = ss;
    if (!j.eobrun) {
        for (; k <= se; k++) {
            int rs = decode(r, j.ac[c.ta]);
            int run = rs >> 4, s = rs & 15;
            int val = 0;
            if (s) {
                val = r.bit() ? p1 : m1;
            } else if (run != 15) {
                j.eobrun = 1 << run;
                if (run) j.eobrun += (int)r.get(run);
                break;
            }
            // avanza por los coeficientes ya distintos de cero (refinándolos) y salta 'run' ceros
            while (k <= se) {
                int16_t* coef = &blk[ZZ[k]];
                if (*coef) {
                    if (r.bit() && !(*coef & p1)) *coef = (int16_t)(*coef + (*coef >= 0 ? p1 : m1));
                } else {
                    if (run-- == 0) break;
                }
                k++;
            }
            if (val && k <= 63) blk[ZZ[k]] = (int16_t)val;
        }
    }
    if (j.eobrun > 0) {
        for (; k <= se; k++) {
            int16_t* coef = &blk[ZZ[k]];
            if (*coef && r.bit() && !(*coef & p1)) *coef = (int16_t)(*coef + (*coef >= 0 ? p1 : m1));
        }
        j.eobrun--;
    }
}

// Salta el marcador de reinicio RSTn que debe venir a continuación
void next_restart(Jpeg& j, Reader& r, Comp* sc[], int ns) {
    r.reset();
    while (r.p + 1 < r.end && !(r.p[0] == 0xFF && r.p[1] >= 0xD0 && r.p[1] <= 0xD7)) r.p++;
    if (r.p + 1 < r.end) r.p += 2;
    for (int i = 0; i < ns; i++) sc[i]->dc_pred = 0;
    j.eobrun = 0;
}

bool scan(Jpeg& j, Reader& r, Comp* sc[], int ns, int ss, int se, int ah, int al) {
    j.eobrun = 0;
    for (int i = 0; i < ns; i++) sc[i]->dc_pred = 0;
    auto block = [&](Comp& c, int bx, int by) {
        int16_t* blk = c.coef + ((size_t)by * c.bw + bx) * 64;
        if (!j.progressive) decode_block_baseline(j, r, c, blk);
        else if (ss == 0) {
            if (!ah) decode_dc_first(j, r, c, blk, al);
            else decode_dc_refine(r, blk, al);
        } else {
            if (!ah) decode_ac_first(j, r, c, blk, ss, se, al);
            else decode_ac_refine(j, r, c, blk, ss, se, al);
        }
    };
    int count = 0;
    if (ns == 1) {
        // no entrelazado: los bloques de la componente, sin el relleno de las MCU
        Comp& c = *sc[0];
        int nbx = (c.cw + 7) / 8, nby = (c.ch + 7) / 8;
        for (int by = 0; by < nby; by++)
            for (int bx = 0; bx < nbx; bx++) {
                if (j.restart && count && count % j.restart == 0) next_restart(j, r, sc, ns);
                block(c, bx, by);
                count++;
            }
    } else {
        for (int my = 0; my < j.mcuy; my++)
            for (int mx = 0; mx < j.mcux; mx++) {
                if (j.restart && count && count % j.restart == 0) next_restart(j, r, sc, ns);
                for (int i = 0; i < ns; i++) {
                    Comp& c = *sc[i];
                    for (int v = 0; v < c.v; v++)
                        for (int h = 0; h < c.h; h++) block(c, mx * c.h + h, my * c.v + v);
                }
                count++;
            }
    }
    return true;
}

// --- IDCT entera (método "islow" de libjpeg: 13 bits de precisión)
constexpr int CB = 13, P1 = 2;
constexpr int F0298 = 2446, F0390 = 3196, F0541 = 4433, F0765 = 6270, F0899 = 7373, F1175 = 9633, F1501 = 12299,
              F1847 = 15137, F1961 = 16069, F2053 = 16819, F2562 = 20995, F3072 = 25172;

inline int descale(int64_t x, int n) { return (int)((x + ((int64_t)1 << (n - 1))) >> n); }

void idct(const int16_t* in, const uint16_t* q, uint8_t* out, int stride) {
    int ws[64];
    for (int c = 0; c < 8; c++) {
        int d[8];
        for (int i = 0; i < 8; i++) d[i] = in[i * 8 + c] * q[i * 8 + c];
        if (!d[1] && !d[2] && !d[3] && !d[4] && !d[5] && !d[6] && !d[7]) {
            int dc = d[0] * (1 << P1);
            for (int i = 0; i < 8; i++) ws[i * 8 + c] = dc;
            continue;
        }
        int64_t z2 = d[2], z3 = d[6];
        int64_t z1 = (z2 + z3) * F0541;
        int64_t tmp2 = z1 + z3 * -F1847, tmp3 = z1 + z2 * F0765;
        z2 = d[0];
        z3 = d[4];
        int64_t tmp0 = (z2 + z3) * (1 << CB), tmp1 = (z2 - z3) * (1 << CB);
        int64_t t10 = tmp0 + tmp3, t13 = tmp0 - tmp3, t11 = tmp1 + tmp2, t12 = tmp1 - tmp2;
        tmp0 = d[7];
        tmp1 = d[5];
        tmp2 = d[3];
        tmp3 = d[1];
        z1 = tmp0 + tmp3;
        z2 = tmp1 + tmp2;
        z3 = tmp0 + tmp2;
        int64_t z4 = tmp1 + tmp3, z5 = (z3 + z4) * F1175;
        tmp0 *= F0298; tmp1 *= F2053; tmp2 *= F3072; tmp3 *= F1501;
        z1 *= -F0899; z2 *= -F2562; z3 *= -F1961; z4 *= -F0390;
        z3 += z5; z4 += z5;
        tmp0 += z1 + z3; tmp1 += z2 + z4; tmp2 += z2 + z3; tmp3 += z1 + z4;
        int sh = CB - P1;
        ws[0 * 8 + c] = descale(t10 + tmp3, sh);
        ws[7 * 8 + c] = descale(t10 - tmp3, sh);
        ws[1 * 8 + c] = descale(t11 + tmp2, sh);
        ws[6 * 8 + c] = descale(t11 - tmp2, sh);
        ws[2 * 8 + c] = descale(t12 + tmp1, sh);
        ws[5 * 8 + c] = descale(t12 - tmp1, sh);
        ws[3 * 8 + c] = descale(t13 + tmp0, sh);
        ws[4 * 8 + c] = descale(t13 - tmp0, sh);
    }
    for (int rr = 0; rr < 8; rr++) {
        const int* d = ws + rr * 8;
        uint8_t* o = out + rr * stride;
        int64_t z2 = d[2], z3 = d[6];
        int64_t z1 = (z2 + z3) * F0541;
        int64_t tmp2 = z1 + z3 * -F1847, tmp3 = z1 + z2 * F0765;
        int64_t tmp0 = ((int64_t)d[0] + d[4]) * (1 << CB), tmp1 = ((int64_t)d[0] - d[4]) * (1 << CB);
        int64_t t10 = tmp0 + tmp3, t13 = tmp0 - tmp3, t11 = tmp1 + tmp2, t12 = tmp1 - tmp2;
        tmp0 = d[7];
        tmp1 = d[5];
        tmp2 = d[3];
        tmp3 = d[1];
        z1 = tmp0 + tmp3;
        z2 = tmp1 + tmp2;
        z3 = tmp0 + tmp2;
        int64_t z4 = tmp1 + tmp3, z5 = (z3 + z4) * F1175;
        tmp0 *= F0298; tmp1 *= F2053; tmp2 *= F3072; tmp3 *= F1501;
        z1 *= -F0899; z2 *= -F2562; z3 *= -F1961; z4 *= -F0390;
        z3 += z5; z4 += z5;
        tmp0 += z1 + z3; tmp1 += z2 + z4; tmp2 += z2 + z3; tmp3 += z1 + z4;
        int sh = CB + P1 + 3;
        auto px = [&](int64_t v) { return (uint8_t)clampv(descale(v, sh) + 128, 0, 255); };
        o[0] = px(t10 + tmp3);
        o[7] = px(t10 - tmp3);
        o[1] = px(t11 + tmp2);
        o[6] = px(t11 - tmp2);
        o[2] = px(t12 + tmp1);
        o[5] = px(t12 - tmp1);
        o[3] = px(t13 + tmp0);
        o[4] = px(t13 - tmp0);
    }
}

// --- Remuestreo de una componente submuestreada a tamaño completo (filas de w muestras)
// 2x1 y 2x2 con el filtro triangular de libjpeg; otros factores, por repetición.
void upsample(const Comp& c, const Jpeg& j, uint8_t* dst) {
    int fx = j.hmax / c.h, fy = j.vmax / c.v;
    int pw = c.bw * 8;   // anchura del plano
    int W = j.w, H = j.h;
    if (fx == 1 && fy == 1) {
        for (int y = 0; y < H; y++) memcpy(dst + (size_t)y * W, c.plane + (size_t)y * pw, (size_t)W);
        return;
    }
    if (fx == 2 && (fy == 1 || fy == 2)) {
        int iw = c.cw;   // muestras útiles por fila
        for (int y = 0; y < H; y++) {
            int sy = fy == 2 ? y / 2 : y;
            const uint8_t* r0 = c.plane + (size_t)sy * pw;
            uint8_t* o = dst + (size_t)y * W;
            if (fy == 1) {
                // h2v1: (3*actual + vecino + 1 o 2) / 4
                for (int x = 0; x < iw; x++) {
                    int cur = r0[x] * 3;
                    int left = x > 0 ? r0[x - 1] : r0[x], right = x + 1 < iw ? r0[x + 1] : r0[x];
                    if (2 * x < W) o[2 * x] = (uint8_t)((cur + left + 1) >> 2);
                    if (2 * x + 1 < W) o[2 * x + 1] = (uint8_t)((cur + right + 2) >> 2);
                }
            } else {
                // h2v2: primero en vertical con la fila vecina (3:1), luego en horizontal
                int ny = (y & 1) ? sy + 1 : sy - 1;
                ny = clampv(ny, 0, c.ch - 1);
                const uint8_t* r1 = c.plane + (size_t)ny * pw;
                auto col = [&](int x) { return 3 * r0[x] + r1[x]; };
                for (int x = 0; x < iw; x++) {
                    int cur = col(x);
                    int left = x > 0 ? col(x - 1) : cur, right = x + 1 < iw ? col(x + 1) : cur;
                    if (2 * x < W) o[2 * x] = (uint8_t)((cur * 3 + left + 8) >> 4);
                    if (2 * x + 1 < W) o[2 * x + 1] = (uint8_t)((cur * 3 + right + 7) >> 4);
                }
            }
        }
        return;
    }
    for (int y = 0; y < H; y++) {
        const uint8_t* r0 = c.plane + (size_t)(y / fy) * pw;
        uint8_t* o = dst + (size_t)y * W;
        for (int x = 0; x < W; x++) o[x] = r0[x / fx];
    }
}

inline uint8_t clamp8(int v) { return (uint8_t)clampv(v, 0, 255); }
inline uint8_t blinn(int x, int y) { int t = x * y + 128; return (uint8_t)((t + (t >> 8)) >> 8); }

// Orientación EXIF (APP1): etiqueta 0x0112 del primer IFD
int exif_orientation(const uint8_t* d, int len) {
    if (len < 14 || memcmp(d, "Exif\0\0", 6)) return 1;
    const uint8_t* t = d + 6;
    int tl = len - 6;
    bool le = t[0] == 'I';
    auto r16 = [&](int o) { return o + 2 <= tl ? (le ? le16(t + o) : be16(t + o)) : 0; };
    auto r32 = [&](int o) { return o + 4 <= tl ? (le ? le32(t + o) : be32(t + o)) : 0u; };
    uint32_t ifd = r32(4);
    if (ifd + 2 > (uint32_t)tl) return 1;
    int n = r16((int)ifd);
    for (int i = 0; i < n; i++) {
        int e = (int)ifd + 2 + i * 12;
        if (e + 12 > tl) break;
        if (r16(e) == 0x0112) {
            int v = r16(e + 8);
            return v >= 1 && v <= 8 ? v : 1;
        }
    }
    return 1;
}

bool apply_orientation(Image& img, int o) {
    if (o <= 1) return true;
    int W = img.w, H = img.h;
    bool swap = o >= 5;
    int nw = swap ? H : W, nh = swap ? W : H;
    auto* dst = (uint32_t*)img_alloc((size_t)nw * nh * 4);
    if (!dst) return false;
    const uint32_t* src = img.frames[0].px;
    for (int y = 0; y < nh; y++)
        for (int x = 0; x < nw; x++) {
            int sx, sy;
            switch (o) {
            case 2: sx = W - 1 - x; sy = y; break;
            case 3: sx = W - 1 - x; sy = H - 1 - y; break;
            case 4: sx = x; sy = H - 1 - y; break;
            case 5: sx = y; sy = x; break;
            case 6: sx = y; sy = H - 1 - x; break;
            case 7: sx = W - 1 - y; sy = H - 1 - x; break;
            default: sx = W - 1 - y; sy = x; break;   // 8
            }
            dst[(size_t)y * nw + x] = src[(size_t)sy * W + sx];
        }
    img_free(img.frames[0].px);
    img.frames[0].px = dst;
    img.w = nw;
    img.h = nh;
    return true;
}

void free_jpeg(Jpeg& j) {
    for (int i = 0; i < j.ncomp; i++) {
        img_free(j.comp[i].coef);
        img_free(j.comp[i].plane);
    }
}
}

bool img::decode_jpeg(const uint8_t* d, uint32_t n, Image& out, const char*& err) {
    if (n < 4 || d[0] != 0xFF || d[1] != 0xD8) { err = T("No es un archivo JPEG válido."); return false; }
    auto* jp = (Jpeg*)img_calloc(sizeof(Jpeg));
    if (!jp) { err = T("No hay memoria suficiente para abrir la imagen."); return false; }
    Jpeg& j = *jp;
    j.adobe = -1;
    j.orientation = 1;
    bool frame = false, any_scan = false, ok = true;
    const char* bad = T("El archivo JPEG está dañado.");
    uint32_t pos = 2;
    while (ok && pos + 4 <= n) {
        if (d[pos] != 0xFF) { pos++; continue; }
        uint8_t m = d[pos + 1];
        if (m == 0xFF) { pos++; continue; }
        pos += 2;
        if (m == 0xD9) break;                       // EOI
        if (m == 0x01 || (m >= 0xD0 && m <= 0xD7)) continue;
        if (pos + 2 > n) break;
        int len = be16(d + pos);
        if (len < 2 || pos + len > n) { ok = false; err = bad; break; }
        const uint8_t* s = d + pos + 2;
        int sl = len - 2;
        switch (m) {
        case 0xDB:   // DQT
            for (int o = 0; o < sl;) {
                int pq = s[o] >> 4, tq = s[o] & 3;
                o++;
                for (int k = 0; k < 64 && o < sl; k++) {
                    j.q[tq][ZZ[k]] = pq ? be16(s + o) : s[o];
                    o += pq ? 2 : 1;
                }
            }
            break;
        case 0xC4:   // DHT
            for (int o = 0; o + 17 <= sl;) {
                int tc = s[o] >> 4, th = s[o] & 3;
                const uint8_t* counts = s + o + 1;
                int total = 0;
                for (int i = 0; i < 16; i++) total += counts[i];
                if (total > 256 || o + 17 + total > sl) { ok = false; err = bad; break; }
                if (!build_huff(tc ? j.ac[th] : j.dc[th], counts, s + o + 17, total)) { ok = false; err = bad; break; }
                o += 17 + total;
            }
            break;
        case 0xDD:   // DRI
            j.restart = be16(s);
            break;
        case 0xE0:
            if (sl >= 5 && !memcmp(s, "JFIF", 5)) j.jfif = true;
            break;
        case 0xE1:
            j.orientation = exif_orientation(s, sl);
            break;
        case 0xEE:
            if (sl >= 12 && !memcmp(s, "Adobe", 5)) j.adobe = s[11];
            break;
        case 0xC0: case 0xC1: case 0xC2: {   // SOF: secuencial o progresivo (Huffman, 8 bits)
            if (frame || sl < 6 || s[0] != 8) { ok = false; err = T("Este tipo de JPEG no está admitido."); break; }
            frame = true;
            j.progressive = m == 0xC2;
            j.h = be16(s + 1);
            j.w = be16(s + 3);
            j.ncomp = s[5];
            if (j.ncomp != 1 && j.ncomp != 3 && j.ncomp != 4) { ok = false; err = T("Este tipo de JPEG no está admitido."); break; }
            if (j.w <= 0 || j.h <= 0 || j.w > MAX_SIDE || j.h > MAX_SIDE || (uint64_t)j.w * j.h > MAX_PIXELS) {
                ok = false;
                err = T("La imagen es demasiado grande.");
                break;
            }
            if (sl < 6 + j.ncomp * 3) { ok = false; err = bad; break; }
            for (int i = 0; i < j.ncomp; i++) {
                Comp& c = j.comp[i];
                c.id = s[6 + i * 3];
                c.h = s[7 + i * 3] >> 4;
                c.v = s[7 + i * 3] & 15;
                c.tq = s[8 + i * 3] & 3;
                if (c.h < 1 || c.h > 4 || c.v < 1 || c.v > 4) { ok = false; err = bad; }
                j.hmax = maxv(j.hmax, c.h);
                j.vmax = maxv(j.vmax, c.v);
            }
            if (!ok) break;
            j.mcux = (j.w + 8 * j.hmax - 1) / (8 * j.hmax);
            j.mcuy = (j.h + 8 * j.vmax - 1) / (8 * j.vmax);
            for (int i = 0; i < j.ncomp; i++) {
                Comp& c = j.comp[i];
                c.bw = j.mcux * c.h;
                c.bh = j.mcuy * c.v;
                c.cw = (j.w * c.h + j.hmax - 1) / j.hmax;
                c.ch = (j.h * c.v + j.vmax - 1) / j.vmax;
                c.coef = (int16_t*)img_calloc((size_t)c.bw * c.bh * 64 * 2);
                if (!c.coef) { ok = false; err = T("No hay memoria suficiente para abrir la imagen."); break; }
            }
            break;
        }
        case 0xC3: case 0xC5: case 0xC6: case 0xC7: case 0xC9: case 0xCA: case 0xCB: case 0xCD: case 0xCE: case 0xCF:
            ok = false;
            err = T("Este tipo de JPEG no está admitido (sin pérdida o aritmético).");
            break;
        case 0xDA: {   // SOS
            if (!frame || sl < 1) { ok = false; err = bad; break; }
            int ns = s[0];
            if (ns < 1 || ns > 4 || sl < 1 + ns * 2 + 3) { ok = false; err = bad; break; }
            Comp* sc[4];
            for (int i = 0; i < ns; i++) {
                int cid = s[1 + i * 2];
                sc[i] = nullptr;
                for (int k = 0; k < j.ncomp; k++)
                    if (j.comp[k].id == cid) sc[i] = &j.comp[k];
                if (!sc[i]) { ok = false; err = bad; break; }
                sc[i]->td = s[2 + i * 2] >> 4 & 3;
                sc[i]->ta = s[2 + i * 2] & 3;
            }
            if (!ok) break;
            int ss = s[1 + ns * 2], se = s[2 + ns * 2], ah = s[3 + ns * 2] >> 4, al = s[3 + ns * 2] & 15;
            if (!j.progressive) { ss = 0; se = 63; ah = al = 0; }
            if (ss > se || se > 63) { ok = false; err = bad; break; }
            Reader r{d + pos + len, d + n};
            scan(j, r, sc, ns, ss, se, ah, al);
            any_scan = true;
            // continúa tras los datos del barrido (en el siguiente marcador)
            const uint8_t* q = r.p;
            while (q + 1 < d + n && !(q[0] == 0xFF && q[1] != 0 && !(q[1] >= 0xD0 && q[1] <= 0xD7))) q++;
            pos = (uint32_t)(q - d);
            len = 0;
            break;
        }
        default: break;
        }
        pos += (uint32_t)len;
    }
    if (ok && (!frame || !any_scan)) { ok = false; err = bad; }
    // IDCT de todos los bloques
    for (int i = 0; ok && i < j.ncomp; i++) {
        Comp& c = j.comp[i];
        c.plane = (uint8_t*)img_alloc((size_t)c.bw * 8 * c.bh * 8);
        if (!c.plane) { ok = false; err = T("No hay memoria suficiente para abrir la imagen."); break; }
        int pw = c.bw * 8;
        for (int by = 0; by < c.bh; by++)
            for (int bx = 0; bx < c.bw; bx++)
                idct(c.coef + ((size_t)by * c.bw + bx) * 64, j.q[c.tq], c.plane + (size_t)by * 8 * pw + bx * 8, pw);
        img_free(c.coef);
        c.coef = nullptr;
    }
    uint8_t* full[4] = {};
    if (ok && !alloc_frames(out, j.w, j.h, 1)) { ok = false; err = T("No hay memoria suficiente para abrir la imagen."); }
    for (int i = 0; ok && i < j.ncomp; i++) {
        full[i] = (uint8_t*)img_alloc((size_t)j.w * j.h);
        if (!full[i]) { ok = false; err = T("No hay memoria suficiente para abrir la imagen."); break; }
        upsample(j.comp[i], j, full[i]);
    }
    if (ok) {
        // conversión de color
        uint32_t* px = out.frames[0].px;
        size_t total = (size_t)j.w * j.h;
        bool rgb = j.ncomp == 3 && j.adobe == 0 && !j.jfif;
        for (size_t k = 0; k < total; k++) {
            int r, g, b;
            if (j.ncomp == 1) {
                r = g = b = full[0][k];
            } else {
                int y = full[0][k], cb = full[1][k] - 128, cr = full[2][k] - 128;
                if (rgb || (j.ncomp == 4 && j.adobe == 0)) {
                    r = full[0][k];
                    g = full[1][k];
                    b = full[2][k];
                } else {
                    r = clamp8(y + ((91881 * cr + 32768) >> 16));
                    g = clamp8(y + ((-22554 * cb - 46802 * cr + 32768) >> 16));
                    b = clamp8(y + ((116130 * cb + 32768) >> 16));
                }
                if (j.ncomp == 4) {
                    int kk = full[3][k];
                    if (j.adobe == 2) { r = 255 - r; g = 255 - g; b = 255 - b; }   // YCCK
                    r = blinn(r, kk);
                    g = blinn(g, kk);
                    b = blinn(b, kk);
                }
            }
            px[k] = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        }
        if (!apply_orientation(out, j.orientation)) { ok = false; err = T("No hay memoria suficiente para abrir la imagen."); }
    }
    for (auto* f : full) img_free(f);
    free_jpeg(j);
    img_free(jp);
    if (!ok) {
        free_image(out);
        return false;
    }
    memcpy(out.format, "JPEG", 5);
    return true;
}

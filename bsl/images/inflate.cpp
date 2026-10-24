// DEFLATE (RFC 1951) dentro de un flujo zlib (RFC 1950): lo que usan los PNG.
// Códigos de Huffman canónicos con una tabla de búsqueda rápida de 9 bits.
#include "common.h"

using namespace img;

namespace {
constexpr int FAST_BITS = 9;

struct Huffman {
    uint16_t fast[1 << FAST_BITS];   // símbolo | longitud << 12 (0 = no está en la tabla)
    uint16_t count[16];              // códigos de cada longitud
    uint16_t symbol[288];            // símbolos ordenados por código
    int max_len;
};

struct Bits {
    const uint8_t* p;
    const uint8_t* end;
    uint64_t buf = 0;
    int n = 0;
    bool overrun = false;

    void refill() {
        while (n <= 56) {
            uint64_t b = 0;
            if (p < end) b = *p++;
            else overrun_bytes++;
            buf |= b << n;
            n += 8;
        }
    }
    int overrun_bytes = 0;
    uint32_t get(int k) {
        if (n < k) refill();
        uint32_t v = (uint32_t)(buf & ((1ull << k) - 1));
        buf >>= k;
        n -= k;
        return v;
    }
    void align() {
        int drop = n & 7;
        buf >>= drop;
        n -= drop;
    }
};

uint32_t reverse(uint32_t v, int bits) {
    uint32_t r = 0;
    for (int i = 0; i < bits; i++) r = (r << 1) | ((v >> i) & 1);
    return r;
}

bool build(Huffman& h, const uint8_t* lengths, int n) {
    for (auto& c : h.count) c = 0;
    for (auto& f : h.fast) f = 0;
    for (int i = 0; i < n; i++) h.count[lengths[i]]++;
    h.count[0] = 0;
    // ¿sobresuscrito?
    int left = 1;
    for (int len = 1; len < 16; len++) {
        left <<= 1;
        left -= h.count[len];
        if (left < 0) return false;
    }
    uint16_t offs[16];
    offs[1] = 0;
    for (int len = 1; len < 15; len++) offs[len + 1] = (uint16_t)(offs[len] + h.count[len]);
    for (int i = 0; i < n; i++)
        if (lengths[i]) h.symbol[offs[lengths[i]]++] = (uint16_t)i;
    // tabla rápida: códigos canónicos (MSB primero) invertidos porque DEFLATE lee LSB primero
    h.max_len = 0;
    int code = 0, k = 0;
    for (int len = 1; len < 16; len++) {
        for (int i = 0; i < h.count[len]; i++, k++, code++) {
            h.max_len = len;
            if (len <= FAST_BITS) {
                uint32_t r = reverse((uint32_t)code, len);
                for (uint32_t fill = r; fill < (1u << FAST_BITS); fill += 1u << len)
                    h.fast[fill] = (uint16_t)(h.symbol[k] | (len << 12));
            }
        }
        code <<= 1;
    }
    return true;
}

int decode_sym(Bits& b, const Huffman& h) {
    if (b.n < 16) b.refill();
    uint16_t f = h.fast[b.buf & ((1u << FAST_BITS) - 1)];
    if (f) {
        int len = f >> 12;
        b.buf >>= len;
        b.n -= len;
        return f & 0xFFF;
    }
    // camino lento: bit a bit (códigos de más de 9 bits)
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; len++) {
        code |= (int)b.get(1);
        int count = h.count[len];
        if (code - count < first) return h.symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

const uint16_t LEN_BASE[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const uint8_t LEN_EXTRA[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const uint16_t DIST_BASE[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const uint8_t DIST_EXTRA[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
}

bool img::zlib_inflate(const uint8_t* src, size_t src_len, uint8_t* dst, size_t dst_len, size_t& written) {
    written = 0;
    if (src_len < 2 || (src[0] & 0x0F) != 8 || ((src[0] << 8) | src[1]) % 31 || (src[1] & 0x20)) return false;
    Bits b{src + 2, src + src_len};
    auto* lit = (Huffman*)img_alloc(sizeof(Huffman) * 2);
    if (!lit) return false;
    Huffman* dist = lit + 1;
    size_t out = 0;
    bool ok = false, last = false;
    while (!last) {
        last = b.get(1);
        uint32_t type = b.get(2);
        if (type == 0) {
            // bloque sin comprimir
            b.align();
            uint32_t len = b.get(16), nlen = b.get(16);
            if ((len ^ 0xFFFF) != nlen) goto done;
            // los bytes que queden en el acumulador primero
            while (len && b.n >= 8) {
                if (out >= dst_len) goto done;
                dst[out++] = (uint8_t)b.get(8);
                len--;
            }
            if (len > (size_t)(b.end - b.p) || out + len > dst_len) goto done;
            memcpy(dst + out, b.p, len);
            b.p += len;
            out += len;
            continue;
        }
        if (type == 3) goto done;
        if (type == 1) {
            // códigos fijos
            uint8_t l[288];
            for (int i = 0; i < 144; i++) l[i] = 8;
            for (int i = 144; i < 256; i++) l[i] = 9;
            for (int i = 256; i < 280; i++) l[i] = 7;
            for (int i = 280; i < 288; i++) l[i] = 8;
            build(*lit, l, 288);
            uint8_t d[30];
            for (auto& x : d) x = 5;
            build(*dist, d, 30);
        } else {
            // códigos dinámicos
            int hlit = (int)b.get(5) + 257, hdist = (int)b.get(5) + 1, hclen = (int)b.get(4) + 4;
            static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
            uint8_t cl[19] = {};
            for (int i = 0; i < hclen; i++) cl[order[i]] = (uint8_t)b.get(3);
            Huffman clh;
            if (!build(clh, cl, 19)) goto done;
            uint8_t lens[320] = {};
            int n = 0;
            while (n < hlit + hdist) {
                int sym = decode_sym(b, clh);
                if (sym < 0) goto done;
                if (sym < 16) lens[n++] = (uint8_t)sym;
                else {
                    int rep = 0;
                    uint8_t val = 0;
                    if (sym == 16) {
                        if (!n) goto done;
                        val = lens[n - 1];
                        rep = 3 + (int)b.get(2);
                    } else if (sym == 17) rep = 3 + (int)b.get(3);
                    else rep = 11 + (int)b.get(7);
                    if (n + rep > hlit + hdist) goto done;
                    while (rep--) lens[n++] = val;
                }
            }
            if (!build(*lit, lens, hlit) || !build(*dist, lens + hlit, hdist)) goto done;
        }
        // datos del bloque
        for (;;) {
            int sym = decode_sym(b, *lit);
            if (sym < 0) goto done;
            if (sym < 256) {
                if (out >= dst_len) goto done;
                dst[out++] = (uint8_t)sym;
            } else if (sym == 256) {
                break;
            } else {
                sym -= 257;
                if (sym >= 29) goto done;
                uint32_t len = LEN_BASE[sym] + b.get(LEN_EXTRA[sym]);
                int ds = decode_sym(b, *dist);
                if (ds < 0 || ds >= 30) goto done;
                uint32_t d = DIST_BASE[ds] + b.get(DIST_EXTRA[ds]);
                if (d > out || out + len > dst_len) goto done;
                uint8_t* o = dst + out;
                const uint8_t* s = o - d;
                for (uint32_t i = 0; i < len; i++) o[i] = s[i];
                out += len;
            }
        }
        if (b.overrun_bytes > 8) goto done;
    }
    ok = true;
done:
    img_free(lit);
    written = out;
    return ok;
}

bool img::alloc_frames(Image& img, int w, int h, int count) {
    if (w <= 0 || h <= 0 || w > MAX_SIDE || h > MAX_SIDE || (uint64_t)w * h > MAX_PIXELS || count <= 0) return false;
    img.frames = (ImageFrame*)img_calloc(sizeof(ImageFrame) * (size_t)count);
    if (!img.frames) return false;
    for (int i = 0; i < count; i++) {
        img.frames[i].px = (uint32_t*)img_alloc((size_t)w * h * 4);
        if (!img.frames[i].px) {
            img.frame_count = i;
            free_image(img);
            return false;
        }
    }
    img.w = w;
    img.h = h;
    img.frame_count = count;
    return true;
}

void img::free_image(Image& img) {
    for (int i = 0; i < img.frame_count; i++) img_free(img.frames[i].px);
    img_free(img.frames);
    img.frames = nullptr;
    img.frame_count = 0;
    img.w = img.h = 0;
}

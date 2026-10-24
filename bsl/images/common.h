#pragma once
// Utilidades comunes de images.bsl. Con IMG_HOST se compila también en Linux para comparar
// los decodificadores con otra biblioteca (tools/test_images.py).
#include <stdint.h>
#include <stddef.h>
#include "bsl/images.h"

#ifdef IMG_HOST
#include <stdlib.h>
#include <string.h>
inline void* img_alloc(size_t n) { return malloc(n); }
inline void* img_calloc(size_t n) { return calloc(1, n); }
inline void img_free(void* p) { free(p); }
#define T(x) (x)
#else
#include "mm/memory.h"
#include "lib/string.h"
#include "lib/i18n.h"
inline void* img_alloc(size_t n) { return mm::kmalloc(n); }
inline void* img_calloc(size_t n) { return mm::kcalloc(n); }
inline void img_free(void* p) { mm::kfree(p); }
#endif

namespace img {
constexpr int MAX_SIDE = 16384;
constexpr uint64_t MAX_PIXELS = 64ull << 20;   // 64 Mpx (256 MB por fotograma)

inline uint32_t be32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
inline uint16_t be16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
inline uint32_t le32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
inline uint16_t le16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
template <typename X> inline X clampv(X v, X lo, X hi) { return v < lo ? lo : (v > hi ? hi : v); }
template <typename X> inline X minv(X a, X b) { return a < b ? a : b; }
template <typename X> inline X maxv(X a, X b) { return a > b ? a : b; }

// Reserva 'count' fotogramas de w*h (sin inicializar); false si es demasiado grande
bool alloc_frames(Image& img, int w, int h, int count);
void free_image(Image& img);

// zlib (RFC 1950) con DEFLATE (RFC 1951). 'dst' debe tener sitio para todo el resultado.
bool zlib_inflate(const uint8_t* src, size_t src_len, uint8_t* dst, size_t dst_len, size_t& written);

bool decode_png(const uint8_t* d, uint32_t n, Image& out, const char*& err);
bool decode_jpeg(const uint8_t* d, uint32_t n, Image& out, const char*& err);
bool decode_gif(const uint8_t* d, uint32_t n, Image& out, const char*& err);
bool decode_bmp(const uint8_t* d, uint32_t n, Image& out, const char*& err);
}

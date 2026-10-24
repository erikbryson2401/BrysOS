/* BrysOS: lo que lwIP necesita saber de la plataforma (x86-64, little-endian, sin libc) */
#ifndef BRYS_LWIP_ARCH_CC_H
#define BRYS_LWIP_ARCH_CC_H

#include <stddef.h>
#include <stdint.h>

#define BYTE_ORDER LITTLE_ENDIAN
#define LWIP_NO_INTTYPES_H 1
#define LWIP_NO_CTYPE_H 1
#define X8_F "02x"
#define U16_F "u"
#define S16_F "d"
#define X16_F "x"
#define U32_F "u"
#define S32_F "d"
#define X32_F "x"
#define SZT_F "lu"

void brys_lwip_diag(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void brys_lwip_assert(const char* msg, const char* file, int line) __attribute__((noreturn));
void* brys_lwip_malloc(size_t size);
void* brys_lwip_calloc(size_t n, size_t size);
void brys_lwip_free(void* p);
uint32_t brys_lwip_rand(void);

#define LWIP_PLATFORM_DIAG(x) do { brys_lwip_diag x; } while (0)
#define LWIP_PLATFORM_ASSERT(x) brys_lwip_assert(x, __FILE__, __LINE__)

#endif

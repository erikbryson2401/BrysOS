#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

extern "C" {
void* memcpy(void* dst, const void* src, size_t n);
void* memset(void* dst, int c, size_t n);
void* memmove(void* dst, const void* src, size_t n);
int memcmp(const void* a, const void* b, size_t n);
size_t strlen(const char* s);
int strcmp(const char* a, const char* b);
int strncmp(const char* a, const char* b, size_t n);
char* strcpy(char* d, const char* s);
char* strchr(const char* s, int c);
char* strstr(const char* hay, const char* needle);
}

// Copia segura: siempre termina en '\0'
void strlcpy(char* d, const char* s, size_t cap);
void strlcat(char* d, const char* s, size_t cap);
int stricmp(const char* a, const char* b);
char to_upper(char c);
char to_lower(char c);
// Busca 'needle' dentro de 'hay' sin distinguir mayúsculas
bool str_icontains(const char* hay, const char* needle);
// Comodines estilo DOS: * y ?
bool wildcard_match(const char* pattern, const char* str);

int ksnprintf(char* buf, size_t cap, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
int kvsnprintf(char* buf, size_t cap, const char* fmt, va_list ap);

template <typename T> static inline T min(T a, T b) { return a < b ? a : b; }
template <typename T> static inline T max(T a, T b) { return a > b ? a : b; }
template <typename T> static inline T clamp(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }
template <typename T> static inline T abs_val(T v) { return v < 0 ? -v : v; }

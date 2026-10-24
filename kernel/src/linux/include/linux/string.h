/* BrysOS: funciones de cadenas y memoria (las de BrysOS más las propias de Linux) */
#ifndef _BRYS_LINUX_STRING_H
#define _BRYS_LINUX_STRING_H

#include <linux/types.h>

/* De BrysOS (lib/string.cpp) */
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

/* De brys/base.c */
size_t strnlen(const char* s, size_t max);
char* strncpy(char* d, const char* s, size_t n);
ssize_t strscpy(char* d, const char* s, size_t size);
size_t strlcpy(char* d, const char* s, size_t size);
char* strrchr(const char* s, int c);
char* strcat(char* d, const char* s);
int strcasecmp(const char* a, const char* b);
int strncasecmp(const char* a, const char* b, size_t n);
char* kstrdup(const char* s, gfp_t gfp);
void* kmemdup(const void* src, size_t len, gfp_t gfp);
void* memchr(const void* s, int c, size_t n);
void* memchr_inv(const void* s, int c, size_t n);
char* strsep(char** s, const char* ct);
#define strscpy_pad strscpy
#define memzero_explicit(p, n) memset((p), 0, (n))
#define memset_after(obj, v, member) \
    memset((u8*)(obj) + offsetof(__typeof__(*(obj)), member) + sizeof((obj)->member), (v), \
           sizeof(*(obj)) - offsetof(__typeof__(*(obj)), member) - sizeof((obj)->member))
#define unsafe_memcpy(d, s, n, j) memcpy(d, s, n)
static inline bool str_has_prefix(const char* s, const char* p) { return strncmp(s, p, strlen(p)) == 0; }

#endif

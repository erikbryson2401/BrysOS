/* BrysOS: las funciones de <string.h> que usa lwIP (las da lib/string.cpp del núcleo) */
#ifndef BRYS_LWIP_STRING_H
#define BRYS_LWIP_STRING_H

#include <stddef.h>

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
size_t strnlen(const char* s, size_t max);
char* strncpy(char* d, const char* s, size_t n);

#endif

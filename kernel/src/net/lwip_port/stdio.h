/* BrysOS: lwIP sólo usa de <stdio.h> snprintf (lo da la capa de Linux) */
#ifndef BRYS_LWIP_STDIO_H
#define BRYS_LWIP_STDIO_H
#include <stddef.h>
int snprintf(char* buf, size_t size, const char* fmt, ...);
#endif

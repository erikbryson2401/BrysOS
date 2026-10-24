#pragma once
#include <stdarg.h>

// Registro del kernel: puerto serie COM1 + búfer circular en memoria
void log_init();
void klog(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
[[noreturn]] void panic(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

// Acceso al registro en memoria (para mostrarlo en pantalla)
const char* log_buffer();
unsigned log_length();

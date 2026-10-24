#pragma once
// Lo que el intérprete de AML necesita del sistema: memoria, registro y cadenas. Con AML_HOST
// se compila en Linux para las pruebas.
#ifdef AML_HOST
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
inline void* aml_alloc(size_t n) { return calloc(1, n ? n : 1); }
inline void aml_free(void* p) { free(p); }
#define aml_log(...) fprintf(stderr, __VA_ARGS__)
#define aml_snprintf snprintf
#define aml_vsnprintf vsnprintf
#else
#include <stdarg.h>
#include "mm/memory.h"
#include "lib/string.h"
#include "lib/log.h"
inline void* aml_alloc(size_t n) { return mm::kcalloc(n ? n : 1); }
inline void aml_free(void* p) { mm::kfree(p); }
#define aml_log klog
#define aml_snprintf ksnprintf
#define aml_vsnprintf kvsnprintf
#endif

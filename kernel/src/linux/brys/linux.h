/* Capa de compatibilidad con Linux: lo que el resto de BrysOS (C++) puede llamar */
#ifndef BRYS_LINUX_H
#define BRYS_LINUX_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Arranca la capa: llama a las initcalls de los drivers portados (sólo la primera vez).
 * Devuelve 1 si las ha llamado ahora y 0 si ya estaba en marcha. */
int brys_linux_start(void);

/* Informe de las tarjetas Broadcom encontradas por ssb (chip, núcleos internos, SPROM),
 * en texto con saltos de línea \r\n. Devuelve los bytes escritos. */
size_t brys_ssb_report(char* buf, size_t size);

#ifdef __cplusplus
}
#endif

#endif

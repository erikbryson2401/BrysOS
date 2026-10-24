/* BrysOS: copia con el espacio de usuario (en el núcleo, memcpy) */
#ifndef _BRYS_LINUX_UACCESS_H
#define _BRYS_LINUX_UACCESS_H
#include <linux/string.h>
static inline unsigned long copy_from_user(void* to, const void* from, unsigned long n) { memcpy(to, from, n); return 0; }
static inline unsigned long copy_to_user(void* to, const void* from, unsigned long n) { memcpy(to, from, n); return 0; }
#endif

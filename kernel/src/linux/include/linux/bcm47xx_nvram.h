/* BrysOS: NVRAM de los routers Broadcom (no existe en un PC) */
#ifndef _BRYS_LINUX_BCM47XX_NVRAM_H
#define _BRYS_LINUX_BCM47XX_NVRAM_H
#include <linux/errno.h>
static inline int bcm47xx_nvram_getenv(const char* n, char* v, size_t l) { (void)n; (void)v; (void)l; return -ENOENT; }
static inline int bcm47xx_nvram_init_from_mem(u32 b, u32 l) { (void)b; (void)l; return -ENOTSUPP; }
#endif

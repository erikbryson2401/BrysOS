/* BrysOS: configuración del código portado de Linux (equivale al .config del núcleo).
 * Se incluye antes que nada en cada archivo (-include linux/kconfig.h). */
#ifndef _BRYS_LINUX_KCONFIG_H
#define _BRYS_LINUX_KCONFIG_H

/* Plataforma */
#define CONFIG_X86 1
#define CONFIG_X86_64 1
#define CONFIG_64BIT 1
#define CONFIG_PCI 1
#define CONFIG_HZ 1000

/* Sonics Silicon Backplane (tarjetas Broadcom antiguas, como la BCM4322 del HP dv5) */
#define CONFIG_SSB 1
#define CONFIG_SSB_SPROM 1
#define CONFIG_SSB_PCIHOST_POSSIBLE 1
#define CONFIG_SSB_PCIHOST 1
#define CONFIG_SSB_DRIVER_PCICORE_POSSIBLE 1
#define CONFIG_SSB_DRIVER_PCICORE 1
#define CONFIG_SSB_B43_PCI_BRIDGE 1

/* IS_ENABLED(CONFIG_X): 1 si la opción vale 1 (igual que en Linux) */
#define __ARG_PLACEHOLDER_1 0,
#define __take_second_arg(__ignored, val, ...) val
#define __is_defined(x) ___is_defined(x)
#define ___is_defined(val) ____is_defined(__ARG_PLACEHOLDER_##val)
#define ____is_defined(arg1_or_junk) __take_second_arg(arg1_or_junk 1, 0)
#define IS_BUILTIN(option) __is_defined(option)
#define IS_MODULE(option) 0
#define IS_REACHABLE(option) __is_defined(option)
#define IS_ENABLED(option) __is_defined(option)

#endif

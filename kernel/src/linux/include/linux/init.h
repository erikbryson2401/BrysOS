/* BrysOS: funciones de inicio de los "módulos" portados. Cada initcall va a una sección
 * ordenada por nivel; brys/base.c las llama en orden al arrancar la capa de Linux. */
#ifndef _BRYS_LINUX_INIT_H
#define _BRYS_LINUX_INIT_H

#include <linux/compiler.h>

typedef int (*initcall_t)(void);
typedef void (*exitcall_t)(void);

#define __brys_initcall(fn, level) \
    static initcall_t __initcall_##fn##level __used __section(".linux_initcall." #level) = fn;
#define core_initcall(fn) __brys_initcall(fn, 1)
#define postcore_initcall(fn) __brys_initcall(fn, 2)
#define arch_initcall(fn) __brys_initcall(fn, 3)
#define subsys_initcall(fn) __brys_initcall(fn, 4)
#define fs_initcall(fn) __brys_initcall(fn, 5)
#define device_initcall(fn) __brys_initcall(fn, 6)
#define late_initcall(fn) __brys_initcall(fn, 7)
#define module_init(fn) device_initcall(fn)
#define module_exit(fn) static exitcall_t __exitcall_##fn __maybe_unused = fn;

#endif

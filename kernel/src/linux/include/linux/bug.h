/* BrysOS: BUG y WARN. Un WARN escribe en el registro y sigue; un BUG detiene el sistema. */
#ifndef _BRYS_LINUX_BUG_H
#define _BRYS_LINUX_BUG_H

#include <linux/compiler.h>

void brys_linux_bug(const char* file, int line) __attribute__((noreturn));
void brys_linux_warn(const char* file, int line, const char* cond);

#define BUG() brys_linux_bug(__FILE__, __LINE__)
#define BUG_ON(cond) do { if (unlikely(cond)) BUG(); } while (0)
#define WARN_ON(cond) ({ int __r = !!(cond); if (unlikely(__r)) brys_linux_warn(__FILE__, __LINE__, #cond); unlikely(__r); })
#define WARN_ON_ONCE(cond) ({ static bool __w; int __r = !!(cond); \
    if (unlikely(__r) && !__w) { __w = true; brys_linux_warn(__FILE__, __LINE__, #cond); } unlikely(__r); })
#define WARN(cond, fmt, ...) ({ int __r = !!(cond); \
    if (unlikely(__r)) { brys_linux_warn(__FILE__, __LINE__, #cond); printk(fmt, ##__VA_ARGS__); } unlikely(__r); })
#define WARN_ONCE(cond, fmt, ...) WARN(cond, fmt, ##__VA_ARGS__)
#define WARN_ON_SMP(cond) 0

int printk(const char* fmt, ...) __printf(1, 2);

#endif

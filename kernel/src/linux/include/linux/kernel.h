/* BrysOS: lo más usado de <linux/kernel.h> y sus vecinas (printk, errores, mínimos y
 * máximos, cadenas, comprobaciones). Lo implementa brys/base.c. */
#ifndef _BRYS_LINUX_KERNEL_H
#define _BRYS_LINUX_KERNEL_H

#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/errno.h>
#include <linux/printk.h>
#include <linux/string.h>
#include <linux/minmax.h>
#include <linux/bug.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define container_of(ptr, type, member) ((type*)((char*)(ptr) - __builtin_offsetof(type, member)))
#define container_of_const(ptr, type, member) container_of(ptr, type, member)
#define sizeof_field(t, m) sizeof(((t*)0)->m)
#define typeof_member(t, m) __typeof__(((t*)0)->m)

#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#define DIV_ROUND_CLOSEST(x, d) (((x) + ((d) / 2)) / (d))
#define roundup(x, y) ((((x) + ((y) - 1)) / (y)) * (y))
#define rounddown(x, y) ((x) - ((x) % (y)))
#define ALIGN(x, a) (((x) + ((__typeof__(x))(a) - 1)) & ~((__typeof__(x))(a) - 1))
#define ALIGN_DOWN(x, a) ((x) & ~((__typeof__(x))(a) - 1))
#define IS_ALIGNED(x, a) (((x) & ((__typeof__(x))(a) - 1)) == 0)
#define PTR_ALIGN(p, a) ((__typeof__(p))ALIGN((unsigned long)(p), (a)))
#define upper_32_bits(n) ((u32)(((n) >> 16) >> 16))
#define lower_32_bits(n) ((u32)((n) & 0xffffffff))
#define upper_16_bits(n) ((u16)((n) >> 16))
#define lower_16_bits(n) ((u16)((n) & 0xffff))
#define abs(x) ({ __typeof__(x) _x = (x); _x < 0 ? -_x : _x; })
#define swap(a, b) do { __typeof__(a) __t = (a); (a) = (b); (b) = __t; } while (0)

#define U8_MAX ((u8)~0U)
#define S8_MAX ((s8)(U8_MAX >> 1))
#define S8_MIN ((s8)(-S8_MAX - 1))
#define U16_MAX ((u16)~0U)
#define S16_MAX ((s16)(U16_MAX >> 1))
#define S16_MIN ((s16)(-S16_MAX - 1))
#define U32_MAX ((u32)~0U)
#define S32_MAX ((s32)(U32_MAX >> 1))
#define S32_MIN ((s32)(-S32_MAX - 1))
#define U64_MAX ((u64)~0ULL)
#define INT_MAX ((int)(~0U >> 1))
#define INT_MIN (-INT_MAX - 1)
#define UINT_MAX (~0U)
#define LONG_MAX ((long)(~0UL >> 1))
#define ULONG_MAX (~0UL)

/* Cadenas con formato (admite %pM, %ph, %*ph y %pV como Linux) */
int vsnprintf(char* buf, size_t size, const char* fmt, va_list args);
int snprintf(char* buf, size_t size, const char* fmt, ...) __printf(3, 4);
int scnprintf(char* buf, size_t size, const char* fmt, ...) __printf(3, 4);
int vscnprintf(char* buf, size_t size, const char* fmt, va_list args);
int sprintf(char* buf, const char* fmt, ...) __printf(2, 3);
char* kasprintf(gfp_t gfp, const char* fmt, ...) __printf(2, 3);
unsigned long simple_strtoul(const char* cp, char** endp, unsigned int base);
long simple_strtol(const char* cp, char** endp, unsigned int base);
int kstrtoul(const char* s, unsigned int base, unsigned long* res);
int kstrtouint(const char* s, unsigned int base, unsigned int* res);
int kstrtoint(const char* s, unsigned int base, int* res);
int kstrtou8(const char* s, unsigned int base, u8* res);
int kstrtou16(const char* s, unsigned int base, u16* res);
int kstrtobool(const char* s, bool* res);

#define might_sleep() do { } while (0)
#define might_sleep_if(c) do { } while (0)
#define cond_resched() do { } while (0)
#define in_interrupt() 0
#define in_atomic() 0
#define irqs_disabled() 0
#define lockdep_assert_held(l) do { } while (0)
#define lockdep_assert_not_held(l) do { } while (0)

#define SYSTEM_RUNNING 0
#define SYSTEM_POWER_OFF 2
#define SYSTEM_RESTART 3
#define system_state SYSTEM_RUNNING

#define __round_mask(x, y) ((__typeof__(x))((y) - 1))
#define round_up(x, y) ((((x) - 1) | __round_mask(x, y)) + 1)
#define round_down(x, y) ((x) & ~__round_mask(x, y))

#include <linux/ctype.h>
#include <linux/swab.h>
#include <linux/bitops.h>
#include <linux/jiffies.h>

#endif

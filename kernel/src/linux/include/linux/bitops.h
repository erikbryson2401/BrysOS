/* BrysOS: operaciones con bits (las atómicas no necesitan cerrojo en un solo procesador
 * mientras se desactivan las interrupciones) */
#ifndef _BRYS_LINUX_BITOPS_H
#define _BRYS_LINUX_BITOPS_H

#include <linux/types.h>
#include <linux/brys/irqflags.h>

#define BITS_PER_LONG 64
#define BITS_PER_BYTE 8
#define BIT(nr) (1UL << (nr))
#define BIT_ULL(nr) (1ULL << (nr))
#define BIT_MASK(nr) (1UL << ((nr) % BITS_PER_LONG))
#define BIT_WORD(nr) ((nr) / BITS_PER_LONG)
#define BITS_TO_LONGS(nr) (((nr) + BITS_PER_LONG - 1) / BITS_PER_LONG)
#define GENMASK(h, l) (((~0UL) << (l)) & (~0UL >> (BITS_PER_LONG - 1 - (h))))
#define GENMASK_ULL(h, l) (((~0ULL) << (l)) & (~0ULL >> (63 - (h))))
#define DECLARE_BITMAP(name, bits) unsigned long name[BITS_TO_LONGS(bits)]

static inline void __set_bit(long nr, volatile unsigned long* a) { a[BIT_WORD(nr)] |= BIT_MASK(nr); }
static inline void __clear_bit(long nr, volatile unsigned long* a) { a[BIT_WORD(nr)] &= ~BIT_MASK(nr); }
static inline void __change_bit(long nr, volatile unsigned long* a) { a[BIT_WORD(nr)] ^= BIT_MASK(nr); }
static inline int test_bit(long nr, const volatile unsigned long* a) { return (a[BIT_WORD(nr)] >> (nr % BITS_PER_LONG)) & 1; }
static inline void set_bit(long nr, volatile unsigned long* a) { unsigned long f = brys_irq_save(); __set_bit(nr, a); brys_irq_restore(f); }
static inline void clear_bit(long nr, volatile unsigned long* a) { unsigned long f = brys_irq_save(); __clear_bit(nr, a); brys_irq_restore(f); }
static inline void change_bit(long nr, volatile unsigned long* a) { unsigned long f = brys_irq_save(); __change_bit(nr, a); brys_irq_restore(f); }
static inline int test_and_set_bit(long nr, volatile unsigned long* a) {
    unsigned long f = brys_irq_save(); int o = test_bit(nr, a); __set_bit(nr, a); brys_irq_restore(f); return o;
}
static inline int test_and_clear_bit(long nr, volatile unsigned long* a) {
    unsigned long f = brys_irq_save(); int o = test_bit(nr, a); __clear_bit(nr, a); brys_irq_restore(f); return o;
}
#define __test_and_set_bit test_and_set_bit
#define __test_and_clear_bit test_and_clear_bit
#define clear_bit_unlock clear_bit
#define test_and_set_bit_lock test_and_set_bit

static inline int ffs(int x) { return __builtin_ffs(x); }
static inline int fls(unsigned int x) { return x ? 32 - __builtin_clz(x) : 0; }
static inline int fls64(u64 x) { return x ? 64 - __builtin_clzll(x) : 0; }
static inline unsigned long __ffs(unsigned long w) { return (unsigned long)__builtin_ctzl(w); }
static inline unsigned long __fls(unsigned long w) { return 63 - (unsigned long)__builtin_clzl(w); }
static inline unsigned long ffz(unsigned long w) { return __ffs(~w); }
static inline unsigned int hweight8(unsigned int w) { return (unsigned int)__builtin_popcount(w & 0xff); }
static inline unsigned int hweight16(unsigned int w) { return (unsigned int)__builtin_popcount(w & 0xffff); }
static inline unsigned int hweight32(unsigned int w) { return (unsigned int)__builtin_popcount(w); }
static inline unsigned long hweight64(u64 w) { return (unsigned long)__builtin_popcountll(w); }
#define hweight_long hweight64
static inline int ilog2_u64(u64 n) { return fls64(n) - 1; }
#define ilog2(n) ilog2_u64((u64)(n))
#define is_power_of_2(n) ((n) != 0 && (((n) & ((n) - 1)) == 0))
static inline unsigned long roundup_pow_of_two(unsigned long n) { return 1UL << fls64(n - 1); }
static inline unsigned long rounddown_pow_of_two(unsigned long n) { return 1UL << (fls64(n) - 1); }
static inline u32 rol32(u32 w, unsigned int s) { return (w << (s & 31)) | (w >> ((-s) & 31)); }
static inline u32 ror32(u32 w, unsigned int s) { return (w >> (s & 31)) | (w << ((-s) & 31)); }
static inline u16 rol16(u16 w, unsigned int s) { return (u16)((w << (s & 15)) | (w >> ((-s) & 15))); }
static inline u16 ror16(u16 w, unsigned int s) { return (u16)((w >> (s & 15)) | (w << ((-s) & 15))); }

unsigned long find_first_bit(const unsigned long* a, unsigned long size);
unsigned long find_next_bit(const unsigned long* a, unsigned long size, unsigned long offset);
unsigned long find_first_zero_bit(const unsigned long* a, unsigned long size);
unsigned long find_next_zero_bit(const unsigned long* a, unsigned long size, unsigned long offset);
#define for_each_set_bit(bit, addr, size) \
    for ((bit) = find_first_bit((addr), (size)); (bit) < (size); (bit) = find_next_bit((addr), (size), (bit) + 1))

/* Campos de bits (linux/bitfield.h) */
#define __bf_shf(x) (__builtin_ffsll(x) - 1)
#define FIELD_PREP(mask, val) (((__typeof__(mask))(val) << __bf_shf(mask)) & (mask))
#define FIELD_GET(mask, reg) ((__typeof__(mask))(((reg) & (mask)) >> __bf_shf(mask)))
#define FIELD_FIT(mask, val) (!((((__typeof__(mask))(val)) << __bf_shf(mask)) & ~(mask)))
#define FIELD_MAX(mask) ((__typeof__(mask))((mask) >> __bf_shf(mask)))
#define u16_get_bits(v, mask) ((u16)FIELD_GET(mask, v))
#define u32_get_bits(v, mask) ((u32)FIELD_GET(mask, v))
#define u16_encode_bits(v, mask) ((u16)FIELD_PREP(mask, v))
#define u32_encode_bits(v, mask) ((u32)FIELD_PREP(mask, v))
#define le16_get_bits(v, mask) FIELD_GET(mask, le16_to_cpu(v))
#define le32_get_bits(v, mask) FIELD_GET(mask, le32_to_cpu(v))
#define le16_encode_bits(v, mask) cpu_to_le16(FIELD_PREP(mask, v))
#define le32_encode_bits(v, mask) cpu_to_le32(FIELD_PREP(mask, v))

#endif

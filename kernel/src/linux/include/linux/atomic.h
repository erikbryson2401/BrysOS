/* BrysOS: operaciones atómicas. Con un solo procesador basta con hacerlas sin que las corte
 * una interrupción: las de lectura-modificación-escritura usan instrucciones x86 'lock'. */
#ifndef _BRYS_LINUX_ATOMIC_H
#define _BRYS_LINUX_ATOMIC_H

#include <linux/types.h>
#include <linux/compiler.h>

#define ATOMIC_INIT(i) { (i) }
static inline int atomic_read(const atomic_t* v) { return READ_ONCE(v->counter); }
static inline void atomic_set(atomic_t* v, int i) { WRITE_ONCE(v->counter, i); }
static inline void atomic_add(int i, atomic_t* v) { __atomic_add_fetch(&v->counter, i, __ATOMIC_SEQ_CST); }
static inline void atomic_sub(int i, atomic_t* v) { __atomic_sub_fetch(&v->counter, i, __ATOMIC_SEQ_CST); }
static inline void atomic_inc(atomic_t* v) { atomic_add(1, v); }
static inline void atomic_dec(atomic_t* v) { atomic_sub(1, v); }
static inline int atomic_add_return(int i, atomic_t* v) { return __atomic_add_fetch(&v->counter, i, __ATOMIC_SEQ_CST); }
static inline int atomic_sub_return(int i, atomic_t* v) { return __atomic_sub_fetch(&v->counter, i, __ATOMIC_SEQ_CST); }
#define atomic_inc_return(v) atomic_add_return(1, v)
#define atomic_dec_return(v) atomic_sub_return(1, v)
static inline int atomic_dec_and_test(atomic_t* v) { return atomic_sub_return(1, v) == 0; }
static inline int atomic_inc_and_test(atomic_t* v) { return atomic_add_return(1, v) == 0; }
static inline int atomic_xchg(atomic_t* v, int n) { return __atomic_exchange_n(&v->counter, n, __ATOMIC_SEQ_CST); }
static inline int atomic_cmpxchg(atomic_t* v, int old, int n) {
    __atomic_compare_exchange_n(&v->counter, &old, n, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return old;
}
static inline int atomic_fetch_add(int i, atomic_t* v) { return __atomic_fetch_add(&v->counter, i, __ATOMIC_SEQ_CST); }
static inline int atomic_fetch_or(int i, atomic_t* v) { return __atomic_fetch_or(&v->counter, i, __ATOMIC_SEQ_CST); }
static inline int atomic_add_unless(atomic_t* v, int a, int u) {
    int c = atomic_read(v);
    if (c == u) return 0;
    atomic_add(a, v);
    return 1;
}
#define atomic_inc_not_zero(v) atomic_add_unless((v), 1, 0)
#define atomic_long_t atomic64_t
static inline long atomic64_read(const atomic64_t* v) { return READ_ONCE(v->counter); }
static inline void atomic64_set(atomic64_t* v, long i) { WRITE_ONCE(v->counter, i); }
static inline void atomic64_inc(atomic64_t* v) { __atomic_add_fetch(&v->counter, 1, __ATOMIC_SEQ_CST); }
static inline void atomic64_add(long i, atomic64_t* v) { __atomic_add_fetch(&v->counter, i, __ATOMIC_SEQ_CST); }
static inline long atomic64_inc_return(atomic64_t* v) { return __atomic_add_fetch(&v->counter, 1, __ATOMIC_SEQ_CST); }
#define atomic_long_read atomic64_read
#define atomic_long_set atomic64_set
#define atomic_long_inc atomic64_inc

#define refcount_set(r, n) atomic_set((r), (n))
#define refcount_read(r) atomic_read(r)
#define refcount_inc(r) atomic_inc(r)
#define refcount_dec(r) atomic_dec(r)
#define refcount_dec_and_test(r) atomic_dec_and_test(r)
#define REFCOUNT_INIT(n) ATOMIC_INIT(n)

#define smp_store_release(p, v) do { barrier(); WRITE_ONCE(*(p), (v)); } while (0)
#define smp_load_acquire(p) ({ __typeof__(*(p)) ___v = READ_ONCE(*(p)); barrier(); ___v; })
#define xchg(ptr, v) __atomic_exchange_n((ptr), (v), __ATOMIC_SEQ_CST)
#define cmpxchg(ptr, old, n) ({ __typeof__(*(ptr)) __o = (old); \
    __atomic_compare_exchange_n((ptr), &__o, (n), false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); __o; })

#endif

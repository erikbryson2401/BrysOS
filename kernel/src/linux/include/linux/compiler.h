/* BrysOS: atributos y macros del compilador que usa Linux */
#ifndef _BRYS_LINUX_COMPILER_H
#define _BRYS_LINUX_COMPILER_H

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#define __always_inline inline __attribute__((always_inline))
#define noinline __attribute__((noinline))
#define __packed __attribute__((packed))
#define __aligned(x) __attribute__((aligned(x)))
#define __maybe_unused __attribute__((unused))
#define __always_unused __attribute__((unused))
#define __used __attribute__((used))
#define __cold __attribute__((cold))
#define __printf(a, b) __attribute__((format(printf, a, b)))
#define __weak __attribute__((weak))
#define __noreturn __attribute__((noreturn))
#define __pure __attribute__((pure))
#define __section(s) __attribute__((section(s)))
#define __init
#define __exit
#define __initdata
#define __initconst
#define __devinit
#define __ref
#define __read_mostly
#define __cacheline_aligned __aligned(64)
#define ____cacheline_aligned __aligned(64)
#define fallthrough __attribute__((fallthrough))
#define __counted_by(m)
#define __nonstring
#define __randomize_layout
#define __no_randomize_layout
#define __acquires(x)
#define __releases(x)
#define __must_hold(x)
#define __acquire(x) (void)0
#define __release(x) (void)0
#define __cond_lock(x, c) (c)

#define barrier() __asm__ __volatile__("" ::: "memory")
#define mb() __asm__ __volatile__("mfence" ::: "memory")
#define rmb() __asm__ __volatile__("lfence" ::: "memory")
#define wmb() __asm__ __volatile__("sfence" ::: "memory")
#define smp_mb() barrier()
#define smp_rmb() barrier()
#define smp_wmb() barrier()
#define smp_mb__before_atomic() barrier()
#define smp_mb__after_atomic() barrier()
#define dma_rmb() barrier()
#define dma_wmb() barrier()
#define cpu_relax() __asm__ __volatile__("pause" ::: "memory")

#define READ_ONCE(x) (*(const volatile __typeof__(x)*)&(x))
#define WRITE_ONCE(x, val) do { *(volatile __typeof__(x)*)&(x) = (val); } while (0)
#define ACCESS_ONCE(x) READ_ONCE(x)

#define __same_type(a, b) __builtin_types_compatible_p(__typeof__(a), __typeof__(b))
#define __must_be_array(a) 0
#define BUILD_BUG_ON(cond) _Static_assert(!(cond), "BUILD_BUG_ON: " #cond)
#define BUILD_BUG_ON_ZERO(e) ((int)(sizeof(struct { int : (-!!(e)); })))
#define BUILD_BUG_ON_MSG(cond, msg) _Static_assert(!(cond), msg)
#define BUILD_BUG() do { } while (0)
#define static_assert(expr, ...) _Static_assert(expr, #expr)

#define __stringify_1(x...) #x
#define __stringify(x...) __stringify_1(x)
#define __CONCAT(a, b) a##b
#define CONCATENATE(a, b) __CONCAT(a, b)
#define __UNIQUE_ID(prefix) CONCATENATE(__UNIQUE_ID_##prefix, __COUNTER__)

#endif

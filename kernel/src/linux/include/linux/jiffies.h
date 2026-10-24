/* BrysOS: el reloj de Linux (jiffies) es el de BrysOS, a 1000 Hz */
#ifndef _BRYS_LINUX_JIFFIES_H
#define _BRYS_LINUX_JIFFIES_H

#include <linux/types.h>
#include <brys/glue.h>

#define HZ 1000
#define jiffies ((unsigned long)brys_ticks_ms())
#define jiffies_64 ((u64)brys_ticks_ms())
static inline u64 get_jiffies_64(void) { return brys_ticks_ms(); }
#define MAX_JIFFY_OFFSET ((LONG_MAX >> 1) - 1)

#define time_after(a, b) ((long)((b) - (a)) < 0)
#define time_before(a, b) time_after(b, a)
#define time_after_eq(a, b) ((long)((a) - (b)) >= 0)
#define time_before_eq(a, b) time_after_eq(b, a)
#define time_in_range(a, b, c) (time_after_eq(a, b) && time_before_eq(a, c))
#define time_is_before_jiffies(a) time_after(jiffies, a)
#define time_is_after_jiffies(a) time_before(jiffies, a)
#define time_is_before_eq_jiffies(a) time_after_eq(jiffies, a)
#define time_is_after_eq_jiffies(a) time_before_eq(jiffies, a)
#define time_after64 time_after
#define time_before64 time_before

static inline unsigned long msecs_to_jiffies(unsigned int m) { return m; }
static inline unsigned long usecs_to_jiffies(unsigned int u) { return (u + 999) / 1000; }
static inline unsigned int jiffies_to_msecs(unsigned long j) { return (unsigned int)j; }
static inline unsigned int jiffies_to_usecs(unsigned long j) { return (unsigned int)(j * 1000); }
static inline unsigned long round_jiffies(unsigned long j) { return j; }
static inline unsigned long round_jiffies_relative(unsigned long j) { return j; }
static inline unsigned long round_jiffies_up(unsigned long j) { return j; }
#define secs_to_jiffies(s) ((unsigned long)(s) * HZ)

/* Tiempo en nanosegundos */
typedef s64 ktime_t;
static inline ktime_t ktime_get(void) { return (ktime_t)brys_ns(); }
#define ktime_get_boottime ktime_get
#define ktime_get_real ktime_get
static inline u64 ktime_get_ns(void) { return brys_ns(); }
#define ktime_get_boottime_ns ktime_get_ns
#define ktime_get_real_ns ktime_get_ns
static inline s64 ktime_to_us(ktime_t t) { return t / 1000; }
static inline s64 ktime_to_ms(ktime_t t) { return t / 1000000; }
static inline s64 ktime_to_ns(ktime_t t) { return t; }
static inline ktime_t ktime_sub(ktime_t a, ktime_t b) { return a - b; }
static inline ktime_t ktime_add_us(ktime_t t, u64 us) { return t + (ktime_t)(us * 1000); }
static inline ktime_t ktime_add_ms(ktime_t t, u64 ms) { return t + (ktime_t)(ms * 1000000); }
static inline s64 ktime_us_delta(ktime_t later, ktime_t earlier) { return (later - earlier) / 1000; }
static inline s64 ktime_ms_delta(ktime_t later, ktime_t earlier) { return (later - earlier) / 1000000; }
#define ns_to_ktime(ns) ((ktime_t)(ns))
#define ms_to_ktime(ms) ((ktime_t)(ms) * 1000000)
#define NSEC_PER_USEC 1000L
#define NSEC_PER_MSEC 1000000L
#define NSEC_PER_SEC 1000000000L
#define USEC_PER_MSEC 1000L
#define USEC_PER_SEC 1000000L
#define MSEC_PER_SEC 1000L

#endif

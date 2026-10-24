/* BrysOS: cerrojos. BrysOS tiene un solo procesador y su núcleo no se expropia, así que
 * (como Linux en un solo procesador) basta con desactivar las interrupciones cuando lo pide
 * la variante _irq / _irqsave; las demás sólo marcan el cerrojo. */
#ifndef _BRYS_LINUX_SPINLOCK_H
#define _BRYS_LINUX_SPINLOCK_H

#include <linux/types.h>
#include <linux/compiler.h>
#include <brys/glue.h>

typedef struct { int locked; } spinlock_t;
typedef spinlock_t raw_spinlock_t;
typedef struct { int locked; } rwlock_t;

#define __SPIN_LOCK_UNLOCKED(name) { 0 }
#define DEFINE_SPINLOCK(name) spinlock_t name = { 0 }
#define DEFINE_RAW_SPINLOCK(name) raw_spinlock_t name = { 0 }
#define DEFINE_RWLOCK(name) rwlock_t name = { 0 }
#define spin_lock_init(l) do { (l)->locked = 0; } while (0)
#define raw_spin_lock_init spin_lock_init
#define rwlock_init spin_lock_init

static inline void spin_lock(spinlock_t* l) { l->locked = 1; barrier(); }
static inline void spin_unlock(spinlock_t* l) { barrier(); l->locked = 0; }
static inline int spin_trylock(spinlock_t* l) { if (l->locked) return 0; l->locked = 1; return 1; }
static inline int spin_is_locked(spinlock_t* l) { return l->locked; }
#define spin_lock_bh spin_lock
#define spin_unlock_bh spin_unlock
#define spin_lock_nested(l, s) spin_lock(l)
static inline void spin_lock_irq(spinlock_t* l) { __asm__ __volatile__("cli" ::: "memory"); spin_lock(l); }
static inline void spin_unlock_irq(spinlock_t* l) { spin_unlock(l); __asm__ __volatile__("sti" ::: "memory"); }
#define spin_lock_irqsave(l, flags) do { (flags) = brys_irq_save(); spin_lock(l); } while (0)
#define spin_unlock_irqrestore(l, flags) do { spin_unlock(l); brys_irq_restore(flags); } while (0)
#define raw_spin_lock spin_lock
#define raw_spin_unlock spin_unlock
#define raw_spin_lock_irqsave spin_lock_irqsave
#define raw_spin_unlock_irqrestore spin_unlock_irqrestore
#define assert_spin_locked(l) do { } while (0)
#define read_lock(l) spin_lock((spinlock_t*)(l))
#define read_unlock(l) spin_unlock((spinlock_t*)(l))
#define write_lock(l) spin_lock((spinlock_t*)(l))
#define write_unlock(l) spin_unlock((spinlock_t*)(l))
#define read_lock_bh read_lock
#define read_unlock_bh read_unlock
#define write_lock_bh write_lock
#define write_unlock_bh write_unlock
#define local_irq_save(flags) do { (flags) = brys_irq_save(); } while (0)
#define local_irq_restore(flags) brys_irq_restore(flags)
#define local_irq_disable() __asm__ __volatile__("cli" ::: "memory")
#define local_irq_enable() __asm__ __volatile__("sti" ::: "memory")
#define local_bh_disable() do { } while (0)
#define local_bh_enable() do { } while (0)

#include <linux/mutex.h>

#endif

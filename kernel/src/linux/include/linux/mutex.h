/* BrysOS: mutex de Linux. Si está ocupado, el hilo cede el procesador hasta que se libere. */
#ifndef _BRYS_LINUX_MUTEX_H
#define _BRYS_LINUX_MUTEX_H

#include <linux/types.h>

struct mutex {
    int locked;
    const char* name;
};
#define __MUTEX_INITIALIZER(name) { 0, #name }
#define DEFINE_MUTEX(name) struct mutex name = __MUTEX_INITIALIZER(name)
#define mutex_init(m) do { (m)->locked = 0; (m)->name = #m; } while (0)
void mutex_lock(struct mutex* m);
void mutex_unlock(struct mutex* m);
int mutex_trylock(struct mutex* m);
static inline int mutex_lock_interruptible(struct mutex* m) { mutex_lock(m); return 0; }
static inline int mutex_is_locked(struct mutex* m) { return m->locked; }
#define mutex_lock_nested(m, s) mutex_lock(m)
#define mutex_destroy(m) do { } while (0)
#define guard(type) /* no se usa */

#endif

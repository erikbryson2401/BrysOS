/* BrysOS: colas de trabajo de Linux. Todas las colas comparten el hilo "linux-trabajos"
 * (brys/work.c), que ejecuta los trabajos en orden. */
#ifndef _BRYS_LINUX_WORKQUEUE_H
#define _BRYS_LINUX_WORKQUEUE_H

#include <linux/timer.h>

struct work_struct;
typedef void (*work_func_t)(struct work_struct* work);

struct work_struct {
    struct list_head entry;
    work_func_t func;
    bool pending;
    bool running;
};

struct delayed_work {
    struct work_struct work;
    struct timer_list timer;
};

struct workqueue_struct {
    const char* name;
};

#define INIT_WORK(w, f) do { INIT_LIST_HEAD(&(w)->entry); (w)->func = (f); (w)->pending = false; (w)->running = false; } while (0)
#define __INIT_WORK(w, f, onstack) INIT_WORK(w, f)
void brys_init_delayed_work(struct delayed_work* dw, work_func_t f);
#define INIT_DELAYED_WORK(dw, f) brys_init_delayed_work(dw, f)
#define INIT_DEFERRABLE_WORK INIT_DELAYED_WORK
#define DECLARE_WORK(n, f) struct work_struct n = { LIST_HEAD_INIT((n).entry), (f), false, false }
#define DECLARE_DELAYED_WORK(n, f) struct delayed_work n = { .work = { LIST_HEAD_INIT((n).work.entry), (f), false, false } }
static inline struct delayed_work* to_delayed_work(struct work_struct* w) { return container_of(w, struct delayed_work, work); }

extern struct workqueue_struct* system_wq;
extern struct workqueue_struct* system_long_wq;
extern struct workqueue_struct* system_unbound_wq;
extern struct workqueue_struct* system_freezable_wq;
extern struct workqueue_struct* system_power_efficient_wq;

bool queue_work(struct workqueue_struct* wq, struct work_struct* w);
bool queue_delayed_work(struct workqueue_struct* wq, struct delayed_work* dw, unsigned long delay);
bool mod_delayed_work(struct workqueue_struct* wq, struct delayed_work* dw, unsigned long delay);
static inline bool schedule_work(struct work_struct* w) { return queue_work(system_wq, w); }
static inline bool schedule_delayed_work(struct delayed_work* dw, unsigned long d) { return queue_delayed_work(system_wq, dw, d); }
bool cancel_work_sync(struct work_struct* w);
bool cancel_work(struct work_struct* w);
bool cancel_delayed_work(struct delayed_work* dw);
bool cancel_delayed_work_sync(struct delayed_work* dw);
bool flush_work(struct work_struct* w);
bool flush_delayed_work(struct delayed_work* dw);
void flush_workqueue(struct workqueue_struct* wq);
#define drain_workqueue flush_workqueue
static inline bool work_pending(const struct work_struct* w) { return w->pending; }
static inline bool delayed_work_pending(const struct delayed_work* dw) { return dw->work.pending || dw->timer.pending; }
struct workqueue_struct* alloc_workqueue(const char* fmt, unsigned int flags, int max_active, ...);
#define alloc_ordered_workqueue(fmt, flags, ...) alloc_workqueue(fmt, flags, 1, ##__VA_ARGS__)
#define create_singlethread_workqueue(name) alloc_workqueue(name, 0, 1)
#define create_workqueue(name) alloc_workqueue(name, 0, 1)
void destroy_workqueue(struct workqueue_struct* wq);
#define WQ_UNBOUND 0x2
#define WQ_FREEZABLE 0x4
#define WQ_MEM_RECLAIM 0x8
#define WQ_HIGHPRI 0x10
#define WQ_CPU_INTENSIVE 0x20
#define WQ_SYSFS 0x40
#define WQ_POWER_EFFICIENT 0x80

#endif

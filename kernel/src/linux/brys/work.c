/* Capa de compatibilidad con Linux: trabajo diferido. Un hilo del núcleo, "linux-trabajos",
 * hace lo que en Linux hacen las softirq, ksoftirqd y los kworker: vence temporizadores, ejecuta
 * tasklets y colas de trabajo y sondea las tarjetas de red (NAPI). Las interrupciones sólo
 * apuntan el trabajo y lo despiertan. Además, aquí están request_irq y compañía. */
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include <brys/glue.h>
#include <brys/linux.h>

static LIST_HEAD(timers);
static LIST_HEAD(works);
static LIST_HEAD(tasklets);
static LIST_HEAD(napis);
static volatile int kicked;
static int worker_running;
static void* worker_id;          /* para saber si quien llama es el propio hilo */
static void (*poll_hook)(void);  /* la pila TCP/IP: sus temporizadores, en cada vuelta */
static unsigned poll_interval;
static unsigned long next_poll;

#define IRQ_OFF(f) unsigned long f = brys_irq_save()
#define IRQ_ON(f) brys_irq_restore(f)

static void kick(void) { kicked = 1; }

/* ------------------------------------------------------------------------------------
 * Temporizadores
 * ------------------------------------------------------------------------------------ */
void timer_setup(struct timer_list* t, void (*fn)(struct timer_list*), unsigned int flags) {
    INIT_LIST_HEAD(&t->entry);
    t->function = fn;
    t->flags = flags;
    t->pending = false;
    t->expires = 0;
}

int mod_timer(struct timer_list* t, unsigned long expires) {
    IRQ_OFF(f);
    int was = t->pending;
    if (was) list_del(&t->entry);
    t->expires = expires;
    t->pending = true;
    list_add_tail(&t->entry, &timers);
    kick();
    IRQ_ON(f);
    return was;
}

void add_timer(struct timer_list* t) { mod_timer(t, t->expires); }

int del_timer(struct timer_list* t) {
    IRQ_OFF(f);
    int was = t->pending;
    if (was) { list_del(&t->entry); t->pending = false; }
    IRQ_ON(f);
    return was;
}

/* Saca un temporizador vencido (o NULL) */
static struct timer_list* pop_expired(void) {
    IRQ_OFF(f);
    struct timer_list *t, *found = NULL;
    unsigned long now = jiffies;
    list_for_each_entry(t, &timers, entry) {
        if (time_after_eq(now, t->expires)) { found = t; break; }
    }
    if (found) { list_del(&found->entry); found->pending = false; }
    IRQ_ON(f);
    return found;
}

static int any_timer_due(void) {
    struct timer_list* t;
    unsigned long now = jiffies;
    list_for_each_entry(t, &timers, entry)
        if (time_after_eq(now, t->expires)) return 1;
    return 0;
}

/* ------------------------------------------------------------------------------------
 * Colas de trabajo
 * ------------------------------------------------------------------------------------ */
static struct workqueue_struct the_wq = { "brys" };
struct workqueue_struct* system_wq = &the_wq;
struct workqueue_struct* system_long_wq = &the_wq;
struct workqueue_struct* system_unbound_wq = &the_wq;
struct workqueue_struct* system_freezable_wq = &the_wq;
struct workqueue_struct* system_power_efficient_wq = &the_wq;

struct workqueue_struct* alloc_workqueue(const char* fmt, unsigned int flags, int max_active, ...) {
    (void)flags; (void)max_active;
    struct workqueue_struct* wq = kzalloc(sizeof *wq, GFP_KERNEL);
    if (wq) wq->name = fmt;
    return wq;
}
void destroy_workqueue(struct workqueue_struct* wq) { if (wq != &the_wq) kfree(wq); }

bool queue_work(struct workqueue_struct* wq, struct work_struct* w) {
    (void)wq;
    IRQ_OFF(f);
    bool queued = !w->pending;
    if (queued) {
        w->pending = true;
        list_add_tail(&w->entry, &works);
        kick();
    }
    IRQ_ON(f);
    return queued;
}

static void delayed_work_timer(struct timer_list* t) {
    struct delayed_work* dw = from_timer(dw, t, timer);
    queue_work(system_wq, &dw->work);
}

void brys_init_delayed_work(struct delayed_work* dw, work_func_t fn) {
    INIT_WORK(&dw->work, fn);
    timer_setup(&dw->timer, delayed_work_timer, 0);
}

bool queue_delayed_work(struct workqueue_struct* wq, struct delayed_work* dw, unsigned long delay) {
    if (dw->work.pending || dw->timer.pending) return false;
    if (!delay) return queue_work(wq, &dw->work);
    mod_timer(&dw->timer, jiffies + delay);
    return true;
}

bool mod_delayed_work(struct workqueue_struct* wq, struct delayed_work* dw, unsigned long delay) {
    bool was = cancel_delayed_work(dw);
    queue_delayed_work(wq, dw, delay);
    return was;
}

bool cancel_work(struct work_struct* w) {
    IRQ_OFF(f);
    bool was = w->pending;
    if (was) { list_del_init(&w->entry); w->pending = false; }
    IRQ_ON(f);
    return was;
}

static int work_idle(void* p) { return !((struct work_struct*)p)->running && !((struct work_struct*)p)->pending; }

bool flush_work(struct work_struct* w) {
    bool busy = w->pending || w->running;
    if (busy && brys_current_thread() != worker_id) brys_wait(work_idle, w);
    return busy;
}

bool cancel_work_sync(struct work_struct* w) {
    bool was = cancel_work(w);
    if (w->running && brys_current_thread() != worker_id) brys_wait(work_idle, w);
    return was;
}

bool cancel_delayed_work(struct delayed_work* dw) {
    bool was = del_timer(&dw->timer);
    return cancel_work(&dw->work) || was;
}

bool cancel_delayed_work_sync(struct delayed_work* dw) {
    bool was = del_timer(&dw->timer);
    return cancel_work_sync(&dw->work) || was;
}

bool flush_delayed_work(struct delayed_work* dw) {
    if (del_timer(&dw->timer)) queue_work(system_wq, &dw->work);
    return flush_work(&dw->work);
}

static int queue_empty(void* p) { (void)p; return list_empty(&works); }
void flush_workqueue(struct workqueue_struct* wq) {
    (void)wq;
    if (brys_current_thread() != worker_id && !list_empty(&works)) brys_wait(queue_empty, NULL);
}

static struct work_struct* pop_work(void) {
    IRQ_OFF(f);
    struct work_struct* w = list_first_entry_or_null(&works, struct work_struct, entry);
    if (w) {
        list_del_init(&w->entry);
        w->pending = false;
        w->running = true;
    }
    IRQ_ON(f);
    return w;
}

/* ------------------------------------------------------------------------------------
 * Tasklets
 * ------------------------------------------------------------------------------------ */
void tasklet_setup(struct tasklet_struct* t, void (*cb)(struct tasklet_struct*)) {
    memset(t, 0, sizeof *t);
    INIT_LIST_HEAD(&t->entry);
    t->callback = cb;
    t->use_callback = true;
}

void tasklet_init(struct tasklet_struct* t, void (*fn)(unsigned long), unsigned long data) {
    memset(t, 0, sizeof *t);
    INIT_LIST_HEAD(&t->entry);
    t->func = fn;
    t->data = data;
}

void tasklet_schedule(struct tasklet_struct* t) {
    IRQ_OFF(f);
    if (!t->scheduled) {
        t->scheduled = true;
        list_add_tail(&t->entry, &tasklets);
        kick();
    }
    IRQ_ON(f);
}

void tasklet_kill(struct tasklet_struct* t) {
    IRQ_OFF(f);
    if (t->scheduled) { list_del_init(&t->entry); t->scheduled = false; }
    IRQ_ON(f);
}

static struct tasklet_struct* pop_tasklet(void) {
    IRQ_OFF(f);
    struct tasklet_struct *t, *found = NULL;
    list_for_each_entry(t, &tasklets, entry) {
        if (!t->disabled) { found = t; break; }
    }
    if (found) { list_del_init(&found->entry); found->scheduled = false; }
    IRQ_ON(f);
    return found;
}

/* ------------------------------------------------------------------------------------
 * NAPI
 * ------------------------------------------------------------------------------------ */
void netif_napi_add_weight(struct net_device* dev, struct napi_struct* napi, int (*poll)(struct napi_struct*, int),
                           int weight) {
    INIT_LIST_HEAD(&napi->poll_list);
    INIT_LIST_HEAD(&napi->dev_list);
    napi->poll = poll;
    napi->weight = weight;
    napi->dev = dev;
    /* como en Linux: nace deshabilitado (SCHED puesto) hasta napi_enable */
    napi->state = BIT(NAPI_STATE_SCHED);
    list_add_tail(&napi->dev_list, &dev->napi_list);
}

void netif_napi_del(struct napi_struct* napi) {
    IRQ_OFF(f);
    if (!list_empty(&napi->poll_list)) list_del_init(&napi->poll_list);
    list_del_init(&napi->dev_list);
    IRQ_ON(f);
}

void napi_enable(struct napi_struct* napi) {
    clear_bit(NAPI_STATE_DISABLE, &napi->state);
    clear_bit(NAPI_STATE_SCHED, &napi->state);
}

void napi_disable(struct napi_struct* napi) {
    IRQ_OFF(f);
    set_bit(NAPI_STATE_DISABLE, &napi->state);
    if (!list_empty(&napi->poll_list)) list_del_init(&napi->poll_list);
    set_bit(NAPI_STATE_SCHED, &napi->state);
    IRQ_ON(f);
}

bool napi_schedule_prep(struct napi_struct* napi) {
    if (test_bit(NAPI_STATE_DISABLE, &napi->state)) return false;
    return !test_and_set_bit(NAPI_STATE_SCHED, &napi->state);
}

void __napi_schedule(struct napi_struct* napi) {
    IRQ_OFF(f);
    if (list_empty(&napi->poll_list)) list_add_tail(&napi->poll_list, &napis);
    kick();
    IRQ_ON(f);
}

bool napi_complete_done(struct napi_struct* napi, int work_done) {
    (void)work_done;
    clear_bit(NAPI_STATE_SCHED, &napi->state);
    return true;
}

static struct napi_struct* pop_napi(void) {
    IRQ_OFF(f);
    struct napi_struct* n = list_first_entry_or_null(&napis, struct napi_struct, poll_list);
    if (n) list_del_init(&n->poll_list);
    IRQ_ON(f);
    return n;
}

/* ------------------------------------------------------------------------------------
 * El hilo
 * ------------------------------------------------------------------------------------ */
static int has_work(void* unused) {
    (void)unused;
    return kicked || any_timer_due() || (poll_hook && time_after_eq(jiffies, next_poll));
}

void brys_linux_set_poll_hook(void (*fn)(void), unsigned interval_ms) {
    poll_hook = fn;
    poll_interval = interval_ms ? interval_ms : 100;
    next_poll = jiffies;
    kick();
}
void brys_linux_kick(void) { kick(); }

static void worker(void* unused) {
    (void)unused;
    worker_id = brys_current_thread();
    for (;;) {
        brys_wait(has_work, NULL);
        kicked = 0;
        struct timer_list* t;
        while ((t = pop_expired())) t->function(t);
        struct tasklet_struct* tl;
        while ((tl = pop_tasklet())) {
            if (tl->use_callback) tl->callback(tl);
            else tl->func(tl->data);
        }
        struct work_struct* w;
        while ((w = pop_work())) {
            w->func(w);
            w->running = false;
        }
        /* NAPI: cada tarjeta procesa hasta 'weight' paquetes; si no ha terminado, otra vuelta */
        LIST_HEAD(again);
        struct napi_struct* n;
        while ((n = pop_napi())) {
            int done = n->poll(n, n->weight);
            if (done >= n->weight && test_bit(NAPI_STATE_SCHED, &n->state) &&
                !test_bit(NAPI_STATE_DISABLE, &n->state))
                list_add_tail(&n->poll_list, &again);
        }
        if (!list_empty(&again)) {
            IRQ_OFF(f);
            list_splice_tail(&again, &napis);
            kick();
            IRQ_ON(f);
        }
        if (poll_hook) {
            poll_hook();
            next_poll = jiffies + poll_interval;
        }
        if (kicked) brys_yield();   /* queda trabajo: deja pasar a la interfaz antes de seguir */
    }
}

int brys_linux_start_worker(void) {
    if (worker_running) return 0;
    worker_running = 1;
    return brys_kthread(worker, NULL, "linux-trabajos");
}

/* ------------------------------------------------------------------------------------
 * Interrupciones
 * ------------------------------------------------------------------------------------ */
struct irq_action {
    struct irq_action* next;
    unsigned int irq;
    irq_handler_t handler;
    void* dev_id;
    int disabled;
};
static struct irq_action* actions;

static void irq_trampoline(void* ctx) {
    struct irq_action* a = ctx;
    if (!a->disabled) a->handler((int)a->irq, a->dev_id);
}

int request_irq(unsigned int irq, irq_handler_t handler, unsigned long flags, const char* name, void* dev) {
    (void)flags;
    struct irq_action* a = kzalloc(sizeof *a, GFP_KERNEL);
    if (!a) return -ENOMEM;
    a->irq = irq;
    a->handler = handler;
    a->dev_id = dev;
    if (brys_request_irq((int)irq, irq_trampoline, a)) {
        kfree(a);
        printk(KERN_ERR "request_irq(%u) para %s: no queda sitio en la línea\n", irq, name);
        return -EBUSY;
    }
    a->next = actions;
    actions = a;
    printk(KERN_INFO "IRQ %u: %s\n", irq, name);
    return 0;
}

int request_threaded_irq(unsigned int irq, irq_handler_t handler, irq_handler_t thread_fn, unsigned long flags,
                         const char* name, void* dev) {
    (void)thread_fn;   /* los drivers que usamos no lo necesitan */
    return request_irq(irq, handler, flags, name, dev);
}

void free_irq(unsigned int irq, void* dev) {
    for (struct irq_action** p = &actions; *p; p = &(*p)->next) {
        struct irq_action* a = *p;
        if (a->irq != irq || a->dev_id != dev) continue;
        brys_free_irq((int)irq, irq_trampoline, a);
        *p = a->next;
        kfree(a);
        return;
    }
}

static void set_disabled(unsigned int irq, int d) {
    for (struct irq_action* a = actions; a; a = a->next)
        if (a->irq == irq) a->disabled += d;
}
void disable_irq(unsigned int irq) { set_disabled(irq, 1); }
void disable_irq_nosync(unsigned int irq) { set_disabled(irq, 1); }
void enable_irq(unsigned int irq) { set_disabled(irq, -1); }

/* BrysOS: interrupciones y tasklets de Linux. Los manejadores se llaman desde la IRQ (con
 * las interrupciones desactivadas); los tasklets, desde el hilo "linux-trabajos". */
#ifndef _BRYS_LINUX_INTERRUPT_H
#define _BRYS_LINUX_INTERRUPT_H

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/workqueue.h>

typedef enum irqreturn { IRQ_NONE = 0, IRQ_HANDLED = 1, IRQ_WAKE_THREAD = 2 } irqreturn_t;
#define IRQ_RETVAL(x) ((x) ? IRQ_HANDLED : IRQ_NONE)
typedef irqreturn_t (*irq_handler_t)(int irq, void* dev_id);

#define IRQF_SHARED 0x80
#define IRQF_TRIGGER_NONE 0
#define IRQF_ONESHOT 0x2000
#define IRQF_NO_SUSPEND 0x4000

int request_irq(unsigned int irq, irq_handler_t handler, unsigned long flags, const char* name, void* dev);
int request_threaded_irq(unsigned int irq, irq_handler_t handler, irq_handler_t thread_fn, unsigned long flags,
                         const char* name, void* dev);
void free_irq(unsigned int irq, void* dev);
void disable_irq(unsigned int irq);
void disable_irq_nosync(unsigned int irq);
void enable_irq(unsigned int irq);
static inline void synchronize_irq(unsigned int irq) { (void)irq; }
#define devm_request_irq(dev, irq, h, f, n, d) request_irq(irq, h, f, n, d)

struct tasklet_struct {
    struct list_head entry;
    void (*callback)(struct tasklet_struct* t);
    void (*func)(unsigned long data);
    unsigned long data;
    bool use_callback;
    bool scheduled;
    int disabled;
};
void tasklet_setup(struct tasklet_struct* t, void (*cb)(struct tasklet_struct*));
void tasklet_init(struct tasklet_struct* t, void (*fn)(unsigned long), unsigned long data);
void tasklet_schedule(struct tasklet_struct* t);
#define tasklet_hi_schedule tasklet_schedule
void tasklet_kill(struct tasklet_struct* t);
static inline void tasklet_disable(struct tasklet_struct* t) { t->disabled++; }
static inline void tasklet_enable(struct tasklet_struct* t) { t->disabled--; }
#define from_tasklet(var, t, field) container_of(t, __typeof__(*var), field)

#endif

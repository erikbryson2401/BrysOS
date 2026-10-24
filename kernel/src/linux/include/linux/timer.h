/* BrysOS: temporizadores de Linux. Los ejecuta el hilo "linux-trabajos" (brys/work.c)
 * cuando vence 'expires' (en jiffies = milisegundos). */
#ifndef _BRYS_LINUX_TIMER_H
#define _BRYS_LINUX_TIMER_H

#include <linux/kernel.h>
#include <linux/list.h>

struct timer_list {
    struct list_head entry;
    unsigned long expires;
    void (*function)(struct timer_list*);
    u32 flags;
    bool pending;
};

#define TIMER_DEFERRABLE 0x1
#define TIMER_IRQSAFE 0x2
#define from_timer(var, callback_timer, timer_fieldname) \
    container_of(callback_timer, __typeof__(*var), timer_fieldname)
#define timer_container_of from_timer

void timer_setup(struct timer_list* t, void (*fn)(struct timer_list*), unsigned int flags);
int mod_timer(struct timer_list* t, unsigned long expires);
void add_timer(struct timer_list* t);
int del_timer(struct timer_list* t);
#define del_timer_sync del_timer
#define timer_delete del_timer
#define timer_delete_sync del_timer
#define timer_shutdown_sync del_timer
#define timer_shutdown del_timer
static inline int timer_pending(const struct timer_list* t) { return t->pending; }
#define destroy_timer_on_stack(t) do { } while (0)
#define timer_setup_on_stack timer_setup

#endif

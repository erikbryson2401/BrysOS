/* BrysOS: gestión de energía de dispositivos (no se usa: BrysOS no suspende) */
#ifndef _BRYS_LINUX_PM_H
#define _BRYS_LINUX_PM_H

typedef struct pm_message { int event; } pm_message_t;
struct dev_pm_ops {
    int (*suspend)(void* dev);
    int (*resume)(void* dev);
};
#define PMSG_SUSPEND ((struct pm_message){ .event = 2 })
#define PMSG_ON ((struct pm_message){ .event = 0 })
#define SIMPLE_DEV_PM_OPS(name, suspend_fn, resume_fn) const struct dev_pm_ops name = { 0 }
#define DEFINE_SIMPLE_DEV_PM_OPS SIMPLE_DEV_PM_OPS
#define pm_sleep_ptr(p) NULL
#define pm_ptr(p) NULL

#endif

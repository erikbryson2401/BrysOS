/* BrysOS: watchdog de las placas Broadcom (sólo la estructura que nombra ssb) */
#ifndef _BRYS_LINUX_BCM47XX_WDT_H
#define _BRYS_LINUX_BCM47XX_WDT_H
#include <linux/types.h>
struct bcm47xx_wdt {
    u32 (*timer_set)(struct bcm47xx_wdt*, u32);
    u32 (*timer_set_ms)(struct bcm47xx_wdt*, u32);
    u32 max_timer_ms;
    void* driver_data;
};
static inline void* bcm47xx_wdt_get_drvdata(struct bcm47xx_wdt* w) { return w->driver_data; }
#endif

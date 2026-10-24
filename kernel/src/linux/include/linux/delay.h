/* BrysOS: esperas. udelay/mdelay esperan activamente; msleep cede el procesador. */
#ifndef _BRYS_LINUX_DELAY_H
#define _BRYS_LINUX_DELAY_H

#include <linux/types.h>

void udelay(unsigned long usecs);
void ndelay(unsigned long nsecs);
void mdelay(unsigned long msecs);
void msleep(unsigned int msecs);
unsigned long msleep_interruptible(unsigned int msecs);
void usleep_range(unsigned long min, unsigned long max);
#define fsleep(us) usleep_range(us, us)
#define ssleep(s) msleep((s) * 1000)

#endif

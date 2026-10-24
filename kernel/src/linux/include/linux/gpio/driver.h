/* BrysOS: GPIO de las placas Broadcom empotradas (no se usa en PC) */
#ifndef _BRYS_LINUX_GPIO_DRIVER_H
#define _BRYS_LINUX_GPIO_DRIVER_H
struct gpio_chip { int base; unsigned short ngpio; };
struct irq_domain;
#endif

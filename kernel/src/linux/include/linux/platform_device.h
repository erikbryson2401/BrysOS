/* BrysOS: dispositivos de plataforma (sólo lo que nombra ssb; no se registran) */
#ifndef _BRYS_LINUX_PLATFORM_DEVICE_H
#define _BRYS_LINUX_PLATFORM_DEVICE_H

#include <linux/device.h>

struct platform_device {
    const char* name;
    int id;
    struct device dev;
    u32 num_resources;
    struct resource* resource;
};
static inline int platform_device_register(struct platform_device* p) { (void)p; return 0; }
static inline void platform_device_unregister(struct platform_device* p) { (void)p; }
static inline struct platform_device* platform_device_register_data(struct device* parent, const char* name,
                                                                    int id, const void* data, size_t size) {
    (void)parent; (void)name; (void)id; (void)data; (void)size;
    return NULL;
}
#define PLATFORM_DEVID_NONE (-1)

#endif

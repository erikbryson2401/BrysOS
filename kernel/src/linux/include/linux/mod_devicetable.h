/* BrysOS: tablas de identificadores de dispositivos (para emparejar drivers) */
#ifndef _BRYS_LINUX_MOD_DEVICETABLE_H
#define _BRYS_LINUX_MOD_DEVICETABLE_H

#include <linux/types.h>

typedef unsigned long kernel_ulong_t;

#define PCI_ANY_ID (~0)
struct pci_device_id {
    __u32 vendor, device;
    __u32 subvendor, subdevice;
    __u32 class, class_mask;
    kernel_ulong_t driver_data;
    __u32 override_only;
};

struct ssb_device_id {
    __u16 vendor;
    __u16 coreid;
    __u8 revision;
    __u8 __pad;
} __attribute__((packed, aligned(2)));
#define SSB_DEVICE(_vendor, _coreid, _revision) { .vendor = _vendor, .coreid = _coreid, .revision = _revision, }
#define SSB_ANY_VENDOR 0xFFFF
#define SSB_ANY_ID 0xFFFF
#define SSB_ANY_REV 0xFF

struct bcma_device_id {
    __u16 manuf;
    __u16 id;
    __u8 rev;
    __u8 class;
} __attribute__((packed, aligned(2)));

struct of_device_id {
    char name[32];
    char type[32];
    char compatible[128];
    const void* data;
};

#endif

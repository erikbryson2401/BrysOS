/* BrysOS: dispositivos y drivers PCI de Linux sobre la lista PCI de BrysOS.
 * pci_register_driver recorre los dispositivos, empareja con id_table y llama a probe. */
#ifndef _BRYS_LINUX_PCI_H
#define _BRYS_LINUX_PCI_H

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/mod_devicetable.h>
#include <linux/pci_ids.h>
#include <linux/pci_regs.h>
#include <linux/dma-mapping.h>

typedef int pci_power_t;
#define PCI_D0 0
#define PCI_D3hot 3
#define PCI_D3cold 4
#define PCI_STD_NUM_BARS 6
typedef unsigned int pci_ers_result_t;
typedef int pci_channel_state_t;

struct pci_bus {
    unsigned char number;
};

struct resource {
    resource_size_t start, end;
    const char* name;
    unsigned long flags;
};
#define IORESOURCE_IO 0x100
#define IORESOURCE_MEM 0x200

struct pci_driver;
struct pci_dev;

/* Recuperación de errores del bus (no se usa: sólo la estructura) */
enum pci_ers_result_e { PCI_ERS_RESULT_NONE = 1, PCI_ERS_RESULT_CAN_RECOVER = 2, PCI_ERS_RESULT_NEED_RESET = 3,
                        PCI_ERS_RESULT_DISCONNECT = 4, PCI_ERS_RESULT_RECOVERED = 5 };
enum { pci_channel_io_normal = 1, pci_channel_io_frozen = 2, pci_channel_io_perm_failure = 3 };
struct pci_error_handlers {
    pci_ers_result_t (*error_detected)(struct pci_dev* dev, pci_channel_state_t error);
    pci_ers_result_t (*mmio_enabled)(struct pci_dev* dev);
    pci_ers_result_t (*slot_reset)(struct pci_dev* dev);
    void (*resume)(struct pci_dev* dev);
};

struct pci_dev {
    struct device dev;
    struct pci_bus* bus;
    struct pci_bus brys_bus;
    unsigned int devfn;
    unsigned short vendor, device;
    unsigned short subsystem_vendor, subsystem_device;
    unsigned int class;
    u8 revision;
    u8 hdr_type;
    u8 pcie_cap;
    unsigned int irq;
    struct resource resource[6];
    struct pci_driver* driver;
    u64 dma_mask;
    bool is_busmaster;
    int brys_index;             /* BrysOS: índice en la lista PCI */
    bool brys_enabled;
    bool brys_pcie;
    char brys_name[16];
};
#define to_pci_dev(d) container_of(d, struct pci_dev, dev)

struct pci_driver {
    const char* name;
    const struct pci_device_id* id_table;
    int (*probe)(struct pci_dev* dev, const struct pci_device_id* id);
    void (*remove)(struct pci_dev* dev);
    int (*suspend)(struct pci_dev* dev, pm_message_t state);
    int (*resume)(struct pci_dev* dev);
    void (*shutdown)(struct pci_dev* dev);
    const struct pci_error_handlers* err_handler;
    struct device_driver driver;
    struct list_head brys_node;
};

#define PCI_DEVICE(vend, dev) .vendor = (vend), .device = (dev), .subvendor = PCI_ANY_ID, .subdevice = PCI_ANY_ID
#define PCI_DEVICE_SUB(vend, dev, subvend, subdev) \
    .vendor = (vend), .device = (dev), .subvendor = (subvend), .subdevice = (subdev)
#define PCI_VDEVICE(vend, dev) .vendor = PCI_VENDOR_ID_##vend, .device = (dev), \
    .subvendor = PCI_ANY_ID, .subdevice = PCI_ANY_ID, 0, 0
#define PCI_DEVFN(slot, func) ((((slot) & 0x1f) << 3) | ((func) & 0x07))
#define PCI_SLOT(devfn) (((devfn) >> 3) & 0x1f)
#define PCI_FUNC(devfn) ((devfn) & 0x07)

int pci_register_driver(struct pci_driver* drv);
void pci_unregister_driver(struct pci_driver* drv);
#define module_pci_driver(drv) \
    static int __init drv##_init(void) { return pci_register_driver(&(drv)); } module_init(drv##_init)

int pci_read_config_byte(const struct pci_dev* d, int where, u8* val);
int pci_read_config_word(const struct pci_dev* d, int where, u16* val);
int pci_read_config_dword(const struct pci_dev* d, int where, u32* val);
int pci_write_config_byte(const struct pci_dev* d, int where, u8 val);
int pci_write_config_word(const struct pci_dev* d, int where, u16 val);
int pci_write_config_dword(const struct pci_dev* d, int where, u32 val);
int pci_enable_device(struct pci_dev* d);
void pci_disable_device(struct pci_dev* d);
void pci_set_master(struct pci_dev* d);
void pci_clear_master(struct pci_dev* d);
static inline int pci_request_regions(struct pci_dev* d, const char* name) { (void)d; (void)name; return 0; }
static inline void pci_release_regions(struct pci_dev* d) { (void)d; }
void __iomem* pci_iomap(struct pci_dev* d, int bar, unsigned long maxlen);
#define pci_ioremap_bar(d, bar) pci_iomap(d, bar, 0)
#define pci_enable_device_mem pci_enable_device
int pci_select_bars(struct pci_dev* d, unsigned long flags);
static inline int pci_request_selected_regions(struct pci_dev* d, int bars, const char* name) { (void)d; (void)bars; (void)name; return 0; }
static inline void pci_release_selected_regions(struct pci_dev* d, int bars) { (void)d; (void)bars; }
static inline int pci_set_mwi(struct pci_dev* d) { (void)d; return 0; }
static inline void pci_clear_mwi(struct pci_dev* d) { (void)d; }
static inline int pci_enable_wake(struct pci_dev* d, pci_power_t s, bool en) { (void)d; (void)s; (void)en; return 0; }
static inline int pci_wake_from_d3(struct pci_dev* d, bool en) { (void)d; (void)en; return 0; }
static inline bool pci_channel_offline(struct pci_dev* d) { (void)d; return false; }
static inline int pcix_get_mmrbc(struct pci_dev* d) { (void)d; return -EINVAL; }
static inline int pcix_set_mmrbc(struct pci_dev* d, int m) { (void)d; (void)m; return -EINVAL; }
static inline bool pci_device_is_present(struct pci_dev* d) { (void)d; return true; }
static inline void pci_iounmap(struct pci_dev* d, void __iomem* p) { (void)d; (void)p; }
static inline const char* pci_name(const struct pci_dev* d) { return d->brys_name; }
static inline void* pci_get_drvdata(struct pci_dev* d) { return dev_get_drvdata(&d->dev); }
static inline void pci_set_drvdata(struct pci_dev* d, void* data) { dev_set_drvdata(&d->dev, data); }
static inline bool pci_is_pcie(struct pci_dev* d) { return d->brys_pcie; }
static inline int pci_set_power_state(struct pci_dev* d, pci_power_t s) { (void)d; (void)s; return 0; }
static inline int pci_save_state(struct pci_dev* d) { (void)d; return 0; }
static inline void pci_restore_state(struct pci_dev* d) { (void)d; }
static inline int pci_prepare_to_sleep(struct pci_dev* d) { (void)d; return 0; }
static inline int pci_back_from_sleep(struct pci_dev* d) { (void)d; return 0; }
static inline int pci_enable_msi(struct pci_dev* d) { (void)d; return -ENOSYS; }
static inline void pci_disable_msi(struct pci_dev* d) { (void)d; }
static inline int pcie_capability_read_word(struct pci_dev* d, int pos, u16* val) { (void)d; (void)pos; *val = 0; return 0; }
#define pci_resource_start(d, bar) ((d)->resource[(bar)].start)
#define pci_resource_end(d, bar) ((d)->resource[(bar)].end)
#define pci_resource_len(d, bar) \
    ((d)->resource[(bar)].end ? (d)->resource[(bar)].end - (d)->resource[(bar)].start + 1 : 0)
#define pci_resource_flags(d, bar) ((d)->resource[(bar)].flags)

/* BrysOS: dispositivos PCI que ha tomado algún driver de Linux (para el informe RED) */
int brys_pci_bound_count(void);
struct pci_dev* brys_pci_bound(int i);

/* Modo anfitrión del núcleo PCI de las tarjetas Broadcom (no se usa en x86) */
struct pci_ops {
    int (*read)(struct pci_bus* bus, unsigned int devfn, int where, int size, u32* val);
    int (*write)(struct pci_bus* bus, unsigned int devfn, int where, int size, u32 val);
};
struct pci_controller {
    struct pci_ops* pci_ops;
};

#define PCIBIOS_SUCCESSFUL 0
#define PCIBIOS_DEVICE_NOT_FOUND 0x86

#endif

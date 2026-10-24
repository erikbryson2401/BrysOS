/* BrysOS: modelo de dispositivos de Linux, reducido a lo esencial: dispositivos, buses y
 * drivers; al registrar uno se emparejan (bus->match) y se llama a probe. Sin sysfs. */
#ifndef _BRYS_LINUX_DEVICE_H
#define _BRYS_LINUX_DEVICE_H

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/pm.h>
#include <linux/gfp.h>

struct device;
struct device_driver;
struct kobj_uevent_env;
struct attribute_group;
struct module;

struct bus_type {
    const char* name;
    const char* dev_name;
    int (*match)(struct device* dev, const struct device_driver* drv);
    int (*uevent)(const struct device* dev, struct kobj_uevent_env* env);
    int (*probe)(struct device* dev);
    void (*remove)(struct device* dev);
    void (*shutdown)(struct device* dev);
    int (*suspend)(struct device* dev, pm_message_t state);
    int (*resume)(struct device* dev);
    const struct attribute_group** dev_groups;
    const struct attribute_group** drv_groups;
    const struct dev_pm_ops* pm;
};

struct device_driver {
    const char* name;
    const struct bus_type* bus;
    struct module* owner;
    const char* mod_name;
    bool suppress_bind_attrs;
    const void* of_match_table;
    const void* acpi_match_table;
    int (*probe)(struct device* dev);
    int (*remove)(struct device* dev);
    void (*shutdown)(struct device* dev);
    int (*suspend)(struct device* dev, pm_message_t state);
    int (*resume)(struct device* dev);
    const struct attribute_group** groups;
    const struct attribute_group** dev_groups;
    const struct dev_pm_ops* pm;
    struct list_head brys_node;   /* BrysOS: lista de drivers registrados */
};

struct device_type {
    const char* name;
    void (*release)(struct device* dev);
};

struct device {
    struct device* parent;
    const char* init_name;
    char brys_name[48];
    const struct device_type* type;
    const struct bus_type* bus;
    struct device_driver* driver;
    void* platform_data;
    void* driver_data;
    u64* dma_mask;
    u64 coherent_dma_mask;
    u64 bus_dma_limit;
    void (*release)(struct device* dev);
    const struct attribute_group** groups;
    void* of_node;
    void* fwnode;
    int numa_node;
    bool brys_registered;
    int brys_refs;
    struct list_head brys_node;   /* BrysOS: lista de dispositivos registrados */
};

/* Atributos de sysfs: se aceptan y se ignoran */
struct attribute {
    const char* name;
    umode_t mode;
};
struct attribute_group {
    const char* name;
    struct attribute** attrs;
};
struct device_attribute {
    struct attribute attr;
    ssize_t (*show)(struct device* dev, struct device_attribute* attr, char* buf);
    ssize_t (*store)(struct device* dev, struct device_attribute* attr, const char* buf, size_t count);
};
#define __ATTR(_name, _mode, _show, _store) { .attr = { .name = #_name, .mode = _mode }, .show = _show, .store = _store }
#define __ATTR_RO(_name) __ATTR(_name, 0444, _name##_show, NULL)
#define __ATTR_RW(_name) __ATTR(_name, 0644, _name##_show, _name##_store)
#define DEVICE_ATTR(_name, _mode, _show, _store) struct device_attribute dev_attr_##_name = __ATTR(_name, _mode, _show, _store)
#define DEVICE_ATTR_RO(_name) struct device_attribute dev_attr_##_name = __ATTR_RO(_name)
#define DEVICE_ATTR_RW(_name) struct device_attribute dev_attr_##_name = __ATTR_RW(_name)
#define DEVICE_ATTR_ADMIN_RW(_name) struct device_attribute dev_attr_##_name = __ATTR(_name, 0600, _name##_show, _name##_store)
#define ATTRIBUTE_GROUPS(_name) \
    static const struct attribute_group _name##_group = { .attrs = _name##_attrs }; \
    static const struct attribute_group* _name##_groups[] = { &_name##_group, NULL }
static inline int device_create_file(struct device* d, const struct device_attribute* a) { (void)d; (void)a; return 0; }
static inline void device_remove_file(struct device* d, const struct device_attribute* a) { (void)d; (void)a; }
int sysfs_emit(char* buf, const char* fmt, ...) __printf(2, 3);
int add_uevent_var(struct kobj_uevent_env* env, const char* fmt, ...) __printf(2, 3);

int device_register(struct device* dev);
void device_unregister(struct device* dev);
void device_initialize(struct device* dev);
int device_add(struct device* dev);
void device_del(struct device* dev);
struct device* get_device(struct device* dev);
void put_device(struct device* dev);
static inline bool device_is_registered(struct device* dev) { return dev->brys_registered; }
int dev_set_name(struct device* dev, const char* fmt, ...) __printf(2, 3);
const char* dev_name(const struct device* dev);
const char* dev_driver_string(const struct device* dev);
static inline void* dev_get_drvdata(const struct device* dev) { return dev->driver_data; }
static inline void dev_set_drvdata(struct device* dev, void* data) { dev->driver_data = data; }
static inline void* dev_get_platdata(const struct device* dev) { return dev->platform_data; }
static inline int dev_to_node(struct device* dev) { (void)dev; return -1; }
static inline void device_set_wakeup_enable(struct device* dev, bool en) { (void)dev; (void)en; }
static inline bool device_can_wakeup(struct device* dev) { (void)dev; return false; }

int bus_register(const struct bus_type* bus);
void bus_unregister(const struct bus_type* bus);
int driver_register(struct device_driver* drv);
void driver_unregister(struct device_driver* drv);

/* Memoria liberada al quitar el dispositivo: en BrysOS, memoria normal */
void* devm_kmalloc(struct device* dev, size_t size, gfp_t gfp);
void* devm_kzalloc(struct device* dev, size_t size, gfp_t gfp);
#define devm_kfree(dev, p) kfree(p)
#define devm_kcalloc(dev, n, size, gfp) devm_kzalloc(dev, (n) * (size), gfp)

/* Mensajes con el nombre del dispositivo */
int dev_printk_brys(const char* level, const struct device* dev, const char* fmt, ...) __printf(3, 4);
#define dev_emerg(dev, fmt, ...) dev_printk_brys(KERN_EMERG, dev, fmt, ##__VA_ARGS__)
#define dev_crit(dev, fmt, ...) dev_printk_brys(KERN_CRIT, dev, fmt, ##__VA_ARGS__)
#define dev_alert(dev, fmt, ...) dev_printk_brys(KERN_ALERT, dev, fmt, ##__VA_ARGS__)
#define dev_err(dev, fmt, ...) dev_printk_brys(KERN_ERR, dev, fmt, ##__VA_ARGS__)
#define dev_warn(dev, fmt, ...) dev_printk_brys(KERN_WARNING, dev, fmt, ##__VA_ARGS__)
#define dev_notice(dev, fmt, ...) dev_printk_brys(KERN_NOTICE, dev, fmt, ##__VA_ARGS__)
#define dev_info(dev, fmt, ...) dev_printk_brys(KERN_INFO, dev, fmt, ##__VA_ARGS__)
#define dev_dbg(dev, fmt, ...) ({ if (0) dev_printk_brys(KERN_DEBUG, dev, fmt, ##__VA_ARGS__); 0; })
#define dev_vdbg dev_dbg
#define dev_err_once dev_err
#define dev_warn_once dev_warn
#define dev_info_once dev_info
#define dev_err_ratelimited dev_err
#define dev_warn_ratelimited dev_warn
#define dev_err_probe(dev, err, fmt, ...) ({ dev_err(dev, fmt, ##__VA_ARGS__); (err); })

#endif

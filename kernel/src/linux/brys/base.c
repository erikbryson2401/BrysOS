/* Capa de compatibilidad con Linux: implementación en C de lo que declaran las cabeceras
 * de include/linux (printk, cadenas, memoria, esperas, modelo de dispositivos, PCI, DMA).
 * Por debajo usa brys/glue.h, que es la parte de BrysOS. */
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <brys/glue.h>
#include <brys/linux.h>

/* ------------------------------------------------------------------------------------
 * Formato de cadenas (como lib/vsprintf.c de Linux, con %pM, %ph y %*ph)
 * ------------------------------------------------------------------------------------ */
struct out {
    char* buf;
    size_t size, len;
};

static void put(struct out* o, char c) {
    if (o->len + 1 < o->size) o->buf[o->len] = c;
    o->len++;
}

static void put_str(struct out* o, const char* s, int width, int prec, bool left) {
    int n = 0;
    if (!s) s = "(null)";
    while ((prec < 0 || n < prec) && s[n]) n++;
    if (!left) for (int i = n; i < width; i++) put(o, ' ');
    for (int i = 0; i < n; i++) put(o, s[i]);
    if (left) for (int i = n; i < width; i++) put(o, ' ');
}

static void put_num(struct out* o, unsigned long long v, int base, bool upper, bool neg, bool plus, bool space,
                    int width, int prec, bool zero, bool left, bool alt) {
    char tmp[24];
    const char* digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int n = 0;
    if (v == 0 && prec != 0) tmp[n++] = '0';
    while (v) { tmp[n++] = digits[v % base]; v /= base; }
    int digits_n = n < prec ? prec : n;
    char sign = neg ? '-' : (plus ? '+' : (space ? ' ' : 0));
    int pre = (sign ? 1 : 0) + (alt && base == 16 ? 2 : 0) + (alt && base == 8 ? 1 : 0);
    int pad = width - digits_n - pre;
    if (prec >= 0) zero = false;
    if (!left && !zero) while (pad-- > 0) put(o, ' ');
    if (sign) put(o, sign);
    if (alt && base == 16) { put(o, '0'); put(o, upper ? 'X' : 'x'); }
    if (alt && base == 8) put(o, '0');
    if (!left && zero) while (pad-- > 0) put(o, '0');
    for (int i = n; i < digits_n; i++) put(o, '0');
    while (n) put(o, tmp[--n]);
    if (left) while (pad-- > 0) put(o, ' ');
}

int vsnprintf(char* buf, size_t size, const char* fmt, va_list ap) {
    struct out o = {buf, size, 0};
    for (; *fmt; fmt++) {
        if (*fmt != '%') { put(&o, *fmt); continue; }
        fmt++;
        bool left = false, zero = false, plus = false, space = false, alt = false;
        for (;; fmt++) {
            if (*fmt == '-') left = true;
            else if (*fmt == '0') zero = true;
            else if (*fmt == '+') plus = true;
            else if (*fmt == ' ') space = true;
            else if (*fmt == '#') alt = true;
            else break;
        }
        int width = -1, prec = -1;
        if (*fmt == '*') { width = va_arg(ap, int); fmt++; if (width < 0) { left = true; width = -width; } }
        else { width = 0; while (isdigit(*fmt)) width = width * 10 + (*fmt++ - '0'); }
        if (*fmt == '.') {
            fmt++;
            if (*fmt == '*') { prec = va_arg(ap, int); fmt++; }
            else { prec = 0; while (isdigit(*fmt)) prec = prec * 10 + (*fmt++ - '0'); }
        }
        int lng = 0;   /* -2 hh, -1 h, 1 l, 2 ll/z/t/j */
        for (;; fmt++) {
            if (*fmt == 'h') lng--;
            else if (*fmt == 'l') lng++;
            else if (*fmt == 'z' || *fmt == 't' || *fmt == 'j' || *fmt == 'L') lng = 2;
            else break;
        }
        char c = *fmt;
        switch (c) {
        case 'd': case 'i': {
            long long v = lng >= 1 ? va_arg(ap, long long) : va_arg(ap, int);
            if (lng == -1) v = (short)v;
            if (lng == -2) v = (signed char)v;
            bool neg = v < 0;
            put_num(&o, neg ? -(unsigned long long)v : (unsigned long long)v, 10, false, neg, plus, space,
                    width, prec, zero, left, false);
            break;
        }
        case 'u': case 'x': case 'X': case 'o': {
            unsigned long long v = lng >= 1 ? va_arg(ap, unsigned long long) : va_arg(ap, unsigned int);
            if (lng == -1) v = (unsigned short)v;
            if (lng == -2) v = (unsigned char)v;
            int base = c == 'o' ? 8 : (c == 'u' ? 10 : 16);
            put_num(&o, v, base, c == 'X', false, false, false, width, prec, zero, left, alt);
            break;
        }
        case 'c': put(&o, (char)va_arg(ap, int)); break;
        case 's': put_str(&o, va_arg(ap, const char*), width, prec, left); break;
        case 'p': {
            const u8* p = va_arg(ap, const u8*);
            if (fmt[1] == 'M' || fmt[1] == 'm') {   /* dirección MAC */
                bool colons = fmt[1] == 'M';
                fmt++;
                if (fmt[1] == 'R' || fmt[1] == 'F') fmt++;
                for (int i = 0; i < 6; i++) {
                    if (i && colons) put(&o, ':');
                    put_num(&o, p ? p[i] : 0, 16, false, false, false, false, 2, -1, true, false, false);
                }
            } else if (fmt[1] == 'h') {             /* volcado hexadecimal corto */
                fmt++;
                char sep = ' ';
                if (fmt[1] == 'C' || fmt[1] == 'D' || fmt[1] == 'N') { sep = fmt[1] == 'C' ? ':' : (fmt[1] == 'D' ? '-' : 0); fmt++; }
                int n = width > 0 ? width : 1;
                for (int i = 0; i < n && p; i++) {
                    if (i && sep) put(&o, sep);
                    put_num(&o, p[i], 16, false, false, false, false, 2, -1, true, false, false);
                }
            } else if (fmt[1] == 'V') {             /* struct va_format */
                fmt++;
                struct va_format { const char* fmt; va_list* va; };
                const struct va_format* vf = (const struct va_format*)p;
                va_list copy;
                va_copy(copy, *vf->va);
                char tmp[256];
                vsnprintf(tmp, sizeof tmp, vf->fmt, copy);
                va_end(copy);
                put_str(&o, tmp, 0, -1, false);
            } else {
                while (isalnum(fmt[1])) fmt++;       /* otros %p...: la dirección */
                put(&o, '0'); put(&o, 'x');
                put_num(&o, (unsigned long)p, 16, false, false, false, false, 16, -1, true, false, false);
            }
            break;
        }
        case '%': put(&o, '%'); break;
        default: put(&o, '%'); if (c) put(&o, c); else fmt--; break;
        }
    }
    if (size) buf[o.len < size ? o.len : size - 1] = 0;
    return (int)o.len;
}

int snprintf(char* buf, size_t size, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

int vscnprintf(char* buf, size_t size, const char* fmt, va_list ap) {
    int n = vsnprintf(buf, size, fmt, ap);
    if (!size) return 0;
    return n < (int)size ? n : (int)size - 1;
}

int scnprintf(char* buf, size_t size, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vscnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

int sprintf(char* buf, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, 4096, fmt, ap);
    va_end(ap);
    return n;
}

int sysfs_emit(char* buf, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vscnprintf(buf, PAGE_SIZE, fmt, ap);
    va_end(ap);
    return n;
}

int add_uevent_var(struct kobj_uevent_env* env, const char* fmt, ...) { (void)env; (void)fmt; return 0; }

char* kasprintf(gfp_t gfp, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    char* s = kmalloc((size_t)n + 1, gfp);
    if (!s) return NULL;
    va_start(ap, fmt);
    vsnprintf(s, (size_t)n + 1, fmt, ap);
    va_end(ap);
    return s;
}

/* ------------------------------------------------------------------------------------
 * printk: al registro de BrysOS, línea a línea, con "linux: " delante
 * ------------------------------------------------------------------------------------ */
static char line_buf[512];
static size_t line_len;

static void flush_line(void) {
    if (!line_len) return;
    line_buf[line_len] = 0;
    brys_log(line_buf);
    line_len = 0;
}

int vprintk(const char* fmt, va_list ap) {
    char tmp[512];
    bool cont = false;
    while (fmt[0] == '\001' && fmt[1]) {      /* nivel KERN_* */
        if (fmt[1] == 'c') cont = true;
        fmt += 2;
    }
    int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    unsigned long f = brys_irq_save();
    if (!cont) flush_line();
    for (const char* s = tmp; *s; s++) {
        if (!line_len) {
            const char* pre = "linux: ";
            while (*pre && line_len < sizeof line_buf - 2) line_buf[line_len++] = *pre++;
        }
        if (line_len < sizeof line_buf - 2) line_buf[line_len++] = *s;
        if (*s == '\n') flush_line();
    }
    brys_irq_restore(f);
    return n;
}

int printk(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vprintk(fmt, ap);
    va_end(ap);
    return n;
}

void print_hex_dump_bytes(const char* prefix, int type, const void* buf, size_t len) {
    (void)type;
    const u8* p = buf;
    for (size_t i = 0; i < len; i += 16) {
        char line[80];
        int n = snprintf(line, sizeof line, "%s%04zx:", prefix, i);
        for (size_t j = i; j < len && j < i + 16; j++) n += snprintf(line + n, sizeof line - n, " %02x", p[j]);
        printk("%s\n", line);
    }
}

void dump_stack(void) { printk(KERN_WARNING "(pila no disponible en BrysOS)\n"); }

void brys_linux_bug(const char* file, int line) {
    char msg[160];
    snprintf(msg, sizeof msg, "BUG en %s:%d", file, line);
    brys_panic(msg);
}

void brys_linux_warn(const char* file, int line, const char* cond) {
    printk(KERN_WARNING "AVISO en %s:%d: %s\n", file, line, cond);
}

/* ------------------------------------------------------------------------------------
 * Cadenas
 * ------------------------------------------------------------------------------------ */
size_t strnlen(const char* s, size_t max) {
    size_t n = 0;
    while (n < max && s[n]) n++;
    return n;
}

char* strncpy(char* d, const char* s, size_t n) {
    size_t i = 0;
    for (; i < n && s[i]; i++) d[i] = s[i];
    for (; i < n; i++) d[i] = 0;
    return d;
}

ssize_t strscpy(char* d, const char* s, size_t size) {
    if (!size) return -E2BIG;
    size_t i = 0;
    for (; i + 1 < size && s[i]; i++) d[i] = s[i];
    d[i] = 0;
    return s[i] ? -E2BIG : (ssize_t)i;
}

size_t strlcpy(char* d, const char* s, size_t size) {
    size_t len = strlen(s);
    if (size) {
        size_t n = len >= size ? size - 1 : len;
        memcpy(d, s, n);
        d[n] = 0;
    }
    return len;
}

char* strrchr(const char* s, int c) {
    const char* last = NULL;
    do { if (*s == (char)c) last = s; } while (*s++);
    return (char*)last;
}

char* strcat(char* d, const char* s) {
    strcpy(d + strlen(d), s);
    return d;
}

int strncasecmp(const char* a, const char* b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        int ca = tolower((unsigned char)a[i]), cb = tolower((unsigned char)b[i]);
        if (ca != cb || !ca) return ca - cb;
    }
    return 0;
}

int strcasecmp(const char* a, const char* b) { return strncasecmp(a, b, (size_t)-1); }

char* kstrdup(const char* s, gfp_t gfp) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char* d = kmalloc(n, gfp);
    if (d) memcpy(d, s, n);
    return d;
}

void* kmemdup(const void* src, size_t len, gfp_t gfp) {
    void* d = kmalloc(len, gfp);
    if (d) memcpy(d, src, len);
    return d;
}

void* memchr(const void* s, int c, size_t n) {
    const u8* p = s;
    for (size_t i = 0; i < n; i++) if (p[i] == (u8)c) return (void*)(p + i);
    return NULL;
}

void* memchr_inv(const void* s, int c, size_t n) {
    const u8* p = s;
    for (size_t i = 0; i < n; i++) if (p[i] != (u8)c) return (void*)(p + i);
    return NULL;
}

char* strsep(char** s, const char* ct) {
    char* start = *s;
    if (!start) return NULL;
    char* end = start;
    while (*end && !strchr(ct, *end)) end++;
    if (*end) { *end = 0; *s = end + 1; }
    else *s = NULL;
    return start;
}

unsigned long simple_strtoul(const char* cp, char** endp, unsigned int base) {
    unsigned long v = 0;
    while (isspace(*cp)) cp++;
    if (!base) {
        base = 10;
        if (cp[0] == '0') { base = 8; if (tolower(cp[1]) == 'x' && isxdigit(cp[2])) { base = 16; cp += 2; } }
    } else if (base == 16 && cp[0] == '0' && tolower(cp[1]) == 'x') {
        cp += 2;
    }
    for (;;) {
        int d = hex_to_bin((unsigned char)*cp);
        if (d < 0 || d >= (int)base) break;
        v = v * base + (unsigned long)d;
        cp++;
    }
    if (endp) *endp = (char*)cp;
    return v;
}

long simple_strtol(const char* cp, char** endp, unsigned int base) {
    if (*cp == '-') return -(long)simple_strtoul(cp + 1, endp, base);
    return (long)simple_strtoul(cp, endp, base);
}

int kstrtoul(const char* s, unsigned int base, unsigned long* res) {
    char* end;
    if (*s == '+') s++;
    if (!*s) return -EINVAL;
    unsigned long v = simple_strtoul(s, &end, base);
    if (*end == '\n') end++;
    if (*end || end == s) return -EINVAL;
    *res = v;
    return 0;
}

int kstrtouint(const char* s, unsigned int base, unsigned int* res) {
    unsigned long v;
    int r = kstrtoul(s, base, &v);
    if (r) return r;
    if (v > UINT_MAX) return -ERANGE;
    *res = (unsigned int)v;
    return 0;
}

int kstrtoint(const char* s, unsigned int base, int* res) {
    bool neg = *s == '-';
    unsigned long v;
    int r = kstrtoul(neg ? s + 1 : s, base, &v);
    if (r) return r;
    if (v > (unsigned long)INT_MAX + neg) return -ERANGE;
    *res = neg ? -(int)v : (int)v;
    return 0;
}

int kstrtou8(const char* s, unsigned int base, u8* res) {
    unsigned long v;
    int r = kstrtoul(s, base, &v);
    if (r) return r;
    if (v > U8_MAX) return -ERANGE;
    *res = (u8)v;
    return 0;
}

int kstrtou16(const char* s, unsigned int base, u16* res) {
    unsigned long v;
    int r = kstrtoul(s, base, &v);
    if (r) return r;
    if (v > U16_MAX) return -ERANGE;
    *res = (u16)v;
    return 0;
}

int kstrtobool(const char* s, bool* res) {
    if (!s) return -EINVAL;
    switch (s[0]) {
    case 'y': case 'Y': case 't': case 'T': case '1': *res = true; return 0;
    case 'n': case 'N': case 'f': case 'F': case '0': *res = false; return 0;
    case 'o': case 'O':
        if (s[1] == 'n' || s[1] == 'N') { *res = true; return 0; }
        if (s[1] == 'f' || s[1] == 'F') { *res = false; return 0; }
        break;
    }
    return -EINVAL;
}

/* ------------------------------------------------------------------------------------
 * Bits
 * ------------------------------------------------------------------------------------ */
unsigned long find_next_bit(const unsigned long* a, unsigned long size, unsigned long offset) {
    for (unsigned long i = offset; i < size; i++) if (test_bit((long)i, a)) return i;
    return size;
}
unsigned long find_first_bit(const unsigned long* a, unsigned long size) { return find_next_bit(a, size, 0); }
unsigned long find_next_zero_bit(const unsigned long* a, unsigned long size, unsigned long offset) {
    for (unsigned long i = offset; i < size; i++) if (!test_bit((long)i, a)) return i;
    return size;
}
unsigned long find_first_zero_bit(const unsigned long* a, unsigned long size) { return find_next_zero_bit(a, size, 0); }

/* ------------------------------------------------------------------------------------
 * Memoria
 * ------------------------------------------------------------------------------------ */
void* kmalloc(size_t size, gfp_t flags) {
    void* p = brys_kmalloc(size);
    if (p && (flags & __GFP_ZERO)) memset(p, 0, size);
    return p;
}

void* kzalloc(size_t size, gfp_t flags) {
    void* p = brys_kmalloc(size);
    if (p) memset(p, 0, size);
    (void)flags;
    return p;
}

void* krealloc(const void* p, size_t size, gfp_t flags) {
    (void)flags;
    return brys_krealloc((void*)p, size);
}

void kfree(const void* p) { brys_kfree((void*)p); }

void* devm_kmalloc(struct device* dev, size_t size, gfp_t gfp) { (void)dev; return kmalloc(size, gfp); }
void* devm_kzalloc(struct device* dev, size_t size, gfp_t gfp) { (void)dev; return kzalloc(size, gfp); }

struct kmem_cache* kmem_cache_create(const char* name, unsigned int size, unsigned int align,
                                     unsigned long flags, void (*ctor)(void*)) {
    (void)align; (void)flags; (void)ctor;
    struct kmem_cache* c = kzalloc(sizeof *c, GFP_KERNEL);
    if (c) { c->size = size; c->name = name; }
    return c;
}
void kmem_cache_destroy(struct kmem_cache* c) { kfree(c); }
void* kmem_cache_alloc(struct kmem_cache* c, gfp_t flags) { return kmalloc(c->size, flags); }
void* kmem_cache_zalloc(struct kmem_cache* c, gfp_t flags) { return kzalloc(c->size, flags); }
void kmem_cache_free(struct kmem_cache* c, void* p) { (void)c; kfree(p); }

unsigned long __get_free_pages(gfp_t gfp, unsigned int order) {
    (void)gfp;
    uint64_t phys;
    return (unsigned long)brys_dma_alloc(PAGE_SIZE << order, &phys);
}
void free_pages(unsigned long addr, unsigned int order) { brys_dma_free((void*)addr, PAGE_SIZE << order); }

void* ioremap(phys_addr_t phys, size_t size) { return brys_ioremap(phys, size); }
phys_addr_t virt_to_phys(const volatile void* addr) { return brys_virt_to_phys((const void*)addr); }
void* phys_to_virt(phys_addr_t addr) { return brys_phys_to_virt(addr); }

/* ------------------------------------------------------------------------------------
 * Esperas y mutex
 * ------------------------------------------------------------------------------------ */
void udelay(unsigned long usecs) { brys_udelay(usecs); }
void ndelay(unsigned long nsecs) { brys_udelay((nsecs + 999) / 1000); }
void mdelay(unsigned long msecs) { brys_udelay(msecs * 1000); }
void msleep(unsigned int msecs) { brys_sleep_ms(msecs ? msecs : 1); }
unsigned long msleep_interruptible(unsigned int msecs) { msleep(msecs); return 0; }
void usleep_range(unsigned long min, unsigned long max) {
    (void)max;
    if (min >= 1000) brys_sleep_ms(min / 1000);
    else brys_udelay(min);
}

static int mutex_free(void* m) { return !((struct mutex*)m)->locked; }
void mutex_lock(struct mutex* m) {
    while (m->locked) brys_wait(mutex_free, m);
    m->locked = 1;
}
void mutex_unlock(struct mutex* m) { m->locked = 0; }
int mutex_trylock(struct mutex* m) {
    if (m->locked) return 0;
    m->locked = 1;
    return 1;
}

/* ------------------------------------------------------------------------------------
 * Modelo de dispositivos
 * ------------------------------------------------------------------------------------ */
static LIST_HEAD(all_devices);
static LIST_HEAD(all_drivers);

int bus_register(const struct bus_type* bus) { (void)bus; return 0; }
void bus_unregister(const struct bus_type* bus) { (void)bus; }

const char* dev_name(const struct device* dev) {
    if (dev->brys_name[0]) return dev->brys_name;
    return dev->init_name ? dev->init_name : "(sin nombre)";
}

int dev_set_name(struct device* dev, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(dev->brys_name, sizeof dev->brys_name, fmt, ap);
    va_end(ap);
    return 0;
}

const char* dev_driver_string(const struct device* dev) {
    if (dev->driver && dev->driver->name) return dev->driver->name;
    if (dev->bus && dev->bus->name) return dev->bus->name;
    return "";
}

int dev_printk_brys(const char* level, const struct device* dev, const char* fmt, ...) {
    char msg[384];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    return printk("%s%s %s: %s", level, dev ? dev_driver_string(dev) : "", dev ? dev_name(dev) : "", msg);
}

/* Intenta enlazar un dispositivo con un driver de su mismo bus */
static int try_bind(struct device* dev, struct device_driver* drv) {
    if (dev->driver || !dev->bus || drv->bus != dev->bus) return 0;
    if (dev->bus->match && !dev->bus->match(dev, drv)) return 0;
    dev->driver = drv;
    int err = dev->bus->probe ? dev->bus->probe(dev) : (drv->probe ? drv->probe(dev) : 0);
    if (err) {
        dev->driver = NULL;
        dev_printk_brys(KERN_WARNING, dev, "probe de %s ha fallado (%d)\n", drv->name, err);
        return 0;
    }
    return 1;
}

static void unbind(struct device* dev) {
    if (!dev->driver) return;
    if (dev->bus && dev->bus->remove) dev->bus->remove(dev);
    else if (dev->driver->remove) dev->driver->remove(dev);
    dev->driver = NULL;
}

void device_initialize(struct device* dev) {
    dev->brys_refs = 1;
    INIT_LIST_HEAD(&dev->brys_node);
}

int device_add(struct device* dev) {
    if (!dev->brys_name[0] && dev->init_name) dev_set_name(dev, "%s", dev->init_name);
    list_add_tail(&dev->brys_node, &all_devices);
    dev->brys_registered = true;
    struct device_driver* drv;
    list_for_each_entry(drv, &all_drivers, brys_node)
        if (try_bind(dev, drv)) break;
    return 0;
}

int device_register(struct device* dev) {
    device_initialize(dev);
    return device_add(dev);
}

void device_del(struct device* dev) {
    if (!dev->brys_registered) return;
    unbind(dev);
    list_del_init(&dev->brys_node);
    dev->brys_registered = false;
}

struct device* get_device(struct device* dev) {
    if (dev) dev->brys_refs++;
    return dev;
}

void put_device(struct device* dev) {
    if (!dev || --dev->brys_refs > 0) return;
    if (dev->release) dev->release(dev);
    else if (dev->type && dev->type->release) dev->type->release(dev);
}

void device_unregister(struct device* dev) {
    device_del(dev);
    put_device(dev);
}

int driver_register(struct device_driver* drv) {
    list_add_tail(&drv->brys_node, &all_drivers);
    struct device* dev;
    list_for_each_entry(dev, &all_devices, brys_node) try_bind(dev, drv);
    return 0;
}

void driver_unregister(struct device_driver* drv) {
    struct device* dev;
    list_for_each_entry(dev, &all_devices, brys_node)
        if (dev->driver == drv) unbind(dev);
    list_del(&drv->brys_node);
}

/* ------------------------------------------------------------------------------------
 * PCI
 * ------------------------------------------------------------------------------------ */
#define MAX_BOUND 16
static struct pci_dev* bound[MAX_BOUND];
static int bound_count;

int brys_pci_bound_count(void) { return bound_count; }
struct pci_dev* brys_pci_bound(int i) { return i >= 0 && i < bound_count ? bound[i] : NULL; }

static bool is_bound(int index) {
    for (int i = 0; i < bound_count; i++) if (bound[i]->brys_index == index) return true;
    return false;
}

static const struct pci_device_id* match_id(const struct pci_device_id* ids, const struct brys_pci_info* in) {
    for (; ids && (ids->vendor || ids->subvendor || ids->class_mask); ids++) {
        if (ids->vendor != (u32)PCI_ANY_ID && ids->vendor != in->vendor) continue;
        if (ids->device != (u32)PCI_ANY_ID && ids->device != in->device) continue;
        if (ids->subvendor != (u32)PCI_ANY_ID && ids->subvendor != in->subvendor) continue;
        if (ids->subdevice != (u32)PCI_ANY_ID && ids->subdevice != in->subdevice) continue;
        u32 cls = ((u32)in->cls << 16) | ((u32)in->subclass << 8) | in->prog_if;
        if ((ids->class ^ cls) & ids->class_mask) continue;
        return ids;
    }
    return NULL;
}

static struct pci_dev* new_pci_dev(int index, const struct brys_pci_info* in) {
    struct pci_dev* d = kzalloc(sizeof *d, GFP_KERNEL);
    if (!d) return NULL;
    d->brys_index = index;
    d->brys_bus.number = in->bus;
    d->bus = &d->brys_bus;
    d->devfn = PCI_DEVFN(in->dev, in->func);
    d->vendor = in->vendor;
    d->device = in->device;
    d->subsystem_vendor = in->subvendor;
    d->subsystem_device = in->subdevice;
    d->class = ((u32)in->cls << 16) | ((u32)in->subclass << 8) | in->prog_if;
    d->revision = in->rev;
    d->irq = in->irq;
    d->brys_pcie = in->is_pcie;
    d->dma_mask = DMA_BIT_MASK(32);
    d->dev.dma_mask = &d->dma_mask;
    d->dev.coherent_dma_mask = DMA_BIT_MASK(32);
    snprintf(d->brys_name, sizeof d->brys_name, "0000:%02x:%02x.%u", in->bus, in->dev, in->func);
    device_initialize(&d->dev);
    dev_set_name(&d->dev, "%s", d->brys_name);
    for (int bar = 0; bar < 6; bar++) {
        uint64_t size = 0;
        uint64_t start = brys_pci_bar(index, bar, &size);
        if (!start || !size) continue;
        d->resource[bar].start = start;
        d->resource[bar].end = start + size - 1;
        d->resource[bar].flags = IORESOURCE_MEM;
        u32 lo = brys_pci_read(index, PCI_BASE_ADDRESS_0 + bar * 4, 4);
        if (((lo >> 1) & 3) == 2) bar++;   /* BAR de 64 bits: ocupa dos */
    }
    return d;
}

int pci_register_driver(struct pci_driver* drv) {
    if (!drv->driver.name) drv->driver.name = drv->name;
    INIT_LIST_HEAD(&drv->brys_node);
    int n = brys_pci_count();
    for (int i = 0; i < n && bound_count < MAX_BOUND; i++) {
        struct brys_pci_info in;
        if (brys_pci_info(i, &in) || is_bound(i)) continue;
        const struct pci_device_id* id = match_id(drv->id_table, &in);
        if (!id) continue;
        struct pci_dev* d = new_pci_dev(i, &in);
        if (!d) return -ENOMEM;
        d->driver = drv;
        d->dev.driver = &drv->driver;
        printk(KERN_INFO "pci %s: %04x:%04x, driver %s\n", d->brys_name, in.vendor, in.device, drv->name);
        int err = drv->probe(d, id);
        if (err) {
            printk(KERN_WARNING "pci %s: probe de %s ha fallado (%d)\n", d->brys_name, drv->name, err);
            kfree(d);
            continue;
        }
        bound[bound_count++] = d;
    }
    return 0;
}

void pci_unregister_driver(struct pci_driver* drv) {
    for (int i = 0; i < bound_count; i++) {
        if (bound[i]->driver != drv) continue;
        if (drv->remove) drv->remove(bound[i]);
        kfree(bound[i]);
        bound[i--] = bound[--bound_count];
    }
}

int pci_read_config_byte(const struct pci_dev* d, int where, u8* val) {
    *val = (u8)brys_pci_read(d->brys_index, where, 1);
    return 0;
}
int pci_read_config_word(const struct pci_dev* d, int where, u16* val) {
    *val = (u16)brys_pci_read(d->brys_index, where, 2);
    return 0;
}
int pci_read_config_dword(const struct pci_dev* d, int where, u32* val) {
    *val = brys_pci_read(d->brys_index, where, 4);
    return 0;
}
int pci_write_config_byte(const struct pci_dev* d, int where, u8 val) {
    brys_pci_write(d->brys_index, where, 1, val);
    return 0;
}
int pci_write_config_word(const struct pci_dev* d, int where, u16 val) {
    brys_pci_write(d->brys_index, where, 2, val);
    return 0;
}
int pci_write_config_dword(const struct pci_dev* d, int where, u32 val) {
    brys_pci_write(d->brys_index, where, 4, val);
    return 0;
}

int pci_enable_device(struct pci_dev* d) {
    u16 cmd;
    pci_read_config_word(d, PCI_COMMAND, &cmd);
    cmd |= PCI_COMMAND_MEMORY;
    pci_write_config_word(d, PCI_COMMAND, cmd);
    d->brys_enabled = true;
    return 0;
}

void pci_disable_device(struct pci_dev* d) {
    u16 cmd;
    pci_read_config_word(d, PCI_COMMAND, &cmd);
    pci_write_config_word(d, PCI_COMMAND, cmd & ~(PCI_COMMAND_MASTER));
    d->brys_enabled = false;
}

void pci_set_master(struct pci_dev* d) {
    u16 cmd;
    pci_read_config_word(d, PCI_COMMAND, &cmd);
    pci_write_config_word(d, PCI_COMMAND, cmd | PCI_COMMAND_MASTER);
    d->is_busmaster = true;
}

void pci_clear_master(struct pci_dev* d) {
    u16 cmd;
    pci_read_config_word(d, PCI_COMMAND, &cmd);
    pci_write_config_word(d, PCI_COMMAND, cmd & ~PCI_COMMAND_MASTER);
    d->is_busmaster = false;
}

void __iomem* pci_iomap(struct pci_dev* d, int bar, unsigned long maxlen) {
    resource_size_t len = pci_resource_len(d, bar);
    if (!len) return NULL;
    if (maxlen && maxlen < len) len = maxlen;
    return ioremap(pci_resource_start(d, bar), len);
}

/* ------------------------------------------------------------------------------------
 * DMA
 * ------------------------------------------------------------------------------------ */
void* dma_alloc_coherent(struct device* dev, size_t size, dma_addr_t* handle, gfp_t gfp) {
    (void)dev; (void)gfp;
    uint64_t phys;
    void* v = brys_dma_alloc(size, &phys);
    if (v && handle) *handle = phys;
    return v;
}

void dma_free_coherent(struct device* dev, size_t size, void* cpu, dma_addr_t handle) {
    (void)dev; (void)handle;
    brys_dma_free(cpu, size);
}

dma_addr_t dma_map_single(struct device* dev, void* ptr, size_t size, enum dma_data_direction dir) {
    (void)dev; (void)dir;
    u64 phys = brys_virt_to_phys(ptr);
    /* el búfer tiene que ser contiguo también en memoria física */
    u64 last = (u64)ptr + size - 1;
    if (size && (((u64)ptr ^ last) & ~(PAGE_SIZE - 1)) &&
        brys_virt_to_phys((void*)last) != phys + size - 1) {
        printk(KERN_ERR "dma_map_single: búfer de %zu bytes no contiguo\n", size);
        return DMA_MAPPING_ERROR;
    }
    return phys;
}

int dma_set_mask(struct device* dev, u64 mask) {
    if (mask < DMA_BIT_MASK(32)) return -EIO;   /* toda la memoria para DMA está por debajo de 4 GiB */
    if (dev->dma_mask) *dev->dma_mask = mask;
    return 0;
}
int dma_set_coherent_mask(struct device* dev, u64 mask) {
    if (mask < DMA_BIT_MASK(32)) return -EIO;
    dev->coherent_dma_mask = mask;
    return 0;
}
int dma_set_mask_and_coherent(struct device* dev, u64 mask) {
    int r = dma_set_mask(dev, mask);
    return r ? r : dma_set_coherent_mask(dev, mask);
}

/* ------------------------------------------------------------------------------------
 * Arranque de la capa: llama a las initcalls en orden (una sola vez)
 * ------------------------------------------------------------------------------------ */
extern initcall_t __linux_initcall_start[], __linux_initcall_end[];
static int started;

/* Autoprueba de los hilos del núcleo: duerme, escribe en el registro y termina */
static void thread_selftest(void* arg) {
    u64 t0 = brys_ticks_ms();
    msleep(20);
    printk(KERN_INFO "hilo de prueba %s: ha dormido %llu ms\n", (const char*)arg,
           (unsigned long long)(brys_ticks_ms() - t0));
}

int brys_linux_start(void) {
    if (started) return 0;
    started = 1;
    if (brys_kthread(thread_selftest, "linux-prueba", "linux-prueba"))
        printk(KERN_WARNING "no se ha podido crear el hilo de prueba\n");
    int n = 0;
    for (initcall_t* fn = __linux_initcall_start; fn < __linux_initcall_end; fn++, n++) {
        int err = (*fn)();
        if (err) printk(KERN_WARNING "initcall %d ha devuelto %d\n", n, err);
    }
    printk(KERN_INFO "capa de compatibilidad lista (%d initcalls)\n", n);
    return 1;
}

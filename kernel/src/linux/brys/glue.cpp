// Capa de compatibilidad con Linux: implementación en BrysOS de las funciones de glue.h
#include "linux/brys/glue.h"
#include "arch/idt.h"
#include "arch/io.h"
#include "drivers/pci.h"
#include "fs/vfs.h"
#include "lib/log.h"
#include "lib/string.h"
#include "mm/memory.h"
#include "proc/sched.h"

extern "C" {

void brys_log(const char* text) { klog("%s", text); }
void brys_panic(const char* text) { panic("Linux: %s", text); }

void* brys_kmalloc(size_t size) { return mm::kmalloc(size ? size : 1); }
void* brys_krealloc(void* p, size_t size) { return mm::krealloc(p, size ? size : 1); }
void brys_kfree(void* p) { if (p) mm::kfree(p); }

void* brys_dma_alloc(size_t size, uint64_t* phys) {
    size_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t p = mm::alloc_pages(pages);
    if (!p) return nullptr;
    if (p + pages * PAGE_SIZE > (1ull << 32)) {   // las tarjetas de 32 bits no llegarían
        mm::free_pages(p, pages);
        return nullptr;
    }
    void* v = mm::phys_to_virt(p);
    memset(v, 0, pages * PAGE_SIZE);
    if (phys) *phys = p;
    return v;
}

void brys_dma_free(void* virt, size_t size) {
    if (!virt) return;
    mm::free_pages(mm::virt_to_phys_hhdm(virt), (size + PAGE_SIZE - 1) / PAGE_SIZE);
}

// La memoria del montón y la de DMA están en el mapa directo (HHDM)
uint64_t brys_virt_to_phys(const void* virt) { return mm::vmm_translate((uint64_t)virt); }
void* brys_phys_to_virt(uint64_t phys) { return mm::phys_to_virt(phys); }
void* brys_ioremap(uint64_t phys, size_t size) { return mm::map_mmio(phys, size); }

uint64_t brys_ticks_ms(void) { return pit::ticks(); }

// Nanosegundos con el contador de ciclos (TSC), calibrado con el reloj del sistema la
// primera vez (20 ms de espera)
static uint64_t tsc_per_us = 0;
static uint64_t rdtsc() {
    uint32_t lo, hi;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}
static void calibrate() {
    if (tsc_per_us) return;
    uint64_t t0 = pit::ticks();
    while (pit::ticks() == t0) asm volatile("pause");
    uint64_t c0 = rdtsc(), t1 = pit::ticks();
    while (pit::ticks() < t1 + 20) asm volatile("pause");
    uint64_t c1 = rdtsc();
    tsc_per_us = (c1 - c0) / 20000;
    if (!tsc_per_us) tsc_per_us = 1;
    klog("Linux: TSC a %lu MHz\n", tsc_per_us);
}
uint64_t brys_ns(void) {
    calibrate();
    return rdtsc() * 1000 / tsc_per_us;
}
void brys_udelay(unsigned long us) {
    calibrate();
    uint64_t end = rdtsc() + us * tsc_per_us;
    while (rdtsc() < end) asm volatile("pause");
}
void brys_sleep_ms(unsigned long ms) { sched::sleep((uint32_t)ms); }

void brys_yield(void) { sched::yield(); }
int brys_kthread(void (*fn)(void*), void* arg, const char* name) {
    return sched::create_kernel(fn, arg, name) ? 0 : -1;
}
void brys_wait(int (*cond)(void*), void* ctx) {
    struct Wait { int (*cond)(void*); void* ctx; } w{cond, ctx};
    sched::wait([](void* p) { auto* x = (Wait*)p; return x->cond(x->ctx) != 0; }, &w);
}
unsigned long brys_irq_save(void) { return save_irq(); }
void brys_irq_restore(unsigned long flags) { restore_irq(flags); }
int brys_request_irq(int irq, void (*fn)(void*), void* ctx) { return idt::add_irq_handler(irq, fn, ctx) ? 0 : -1; }
void brys_free_irq(int irq, void (*fn)(void*), void* ctx) { idt::remove_irq_handler(irq, fn, ctx); }

int brys_pci_count(void) { return (int)pci::devices().size(); }

int brys_pci_info(int index, struct brys_pci_info* out) {
    if (index < 0 || index >= brys_pci_count()) return -1;
    const PciDevice& d = pci::devices()[(size_t)index];
    out->bus = d.bus; out->dev = d.dev; out->func = d.func;
    out->vendor = d.vendor; out->device = d.device;
    uint32_t sub = pci::read32(d, 0x2C);
    out->subvendor = sub & 0xFFFF; out->subdevice = sub >> 16;
    out->cls = d.cls; out->subclass = d.subclass; out->prog_if = d.prog_if; out->rev = d.rev;
    out->irq = d.irq;
    // capacidad PCI Express (0x10) en la lista de capacidades
    out->is_pcie = 0;
    if (pci::read16(d, 0x06) & 0x10) {
        uint8_t cap = pci::read32(d, 0x34) & 0xFC;
        for (int n = 0; cap && n < 48; n++) {
            uint32_t v = pci::read32(d, cap);
            if ((v & 0xFF) == 0x10) { out->is_pcie = 1; break; }
            cap = (v >> 8) & 0xFC;
        }
    }
    return 0;
}

uint32_t brys_pci_read(int index, int off, int size) {
    const PciDevice& d = pci::devices()[(size_t)index];
    uint32_t v = pci::read32(d, (uint8_t)(off & ~3));
    int shift = (off & 3) * 8;
    if (size == 1) return (v >> shift) & 0xFF;
    if (size == 2) return (v >> shift) & 0xFFFF;
    return v;
}

void brys_pci_write(int index, int off, int size, uint32_t value) {
    const PciDevice& d = pci::devices()[(size_t)index];
    if (size == 4) { pci::write32(d, (uint8_t)off, value); return; }
    uint32_t v = pci::read32(d, (uint8_t)(off & ~3));
    int shift = (off & 3) * 8;
    uint32_t mask = (size == 1 ? 0xFFu : 0xFFFFu) << shift;
    pci::write32(d, (uint8_t)(off & ~3), (v & ~mask) | ((value << shift) & mask));
}

uint64_t brys_pci_bar(int index, int bar, uint64_t* size) {
    const PciDevice& d = pci::devices()[(size_t)index];
    uint8_t off = (uint8_t)(0x10 + bar * 4);
    uint32_t lo = pci::read32(d, off);
    if (lo & 1) { if (size) *size = 0; return 0; }   // BAR de E/S
    bool is64 = ((lo >> 1) & 3) == 2;
    // tamaño: se escriben unos y se lee lo que admite el dispositivo
    IrqGuard g;
    uint16_t cmd = pci::read16(d, 0x04);
    pci::write16(d, 0x04, (uint16_t)(cmd & ~2));     // sin decodificar memoria mientras tanto
    pci::write32(d, off, 0xFFFFFFFF);
    uint64_t mask = pci::read32(d, off) & ~0xFull;
    pci::write32(d, off, lo);
    if (is64) {
        uint32_t hi = pci::read32(d, (uint8_t)(off + 4));
        pci::write32(d, (uint8_t)(off + 4), 0xFFFFFFFF);
        mask |= (uint64_t)pci::read32(d, (uint8_t)(off + 4)) << 32;
        pci::write32(d, (uint8_t)(off + 4), hi);
    } else {
        mask |= 0xFFFFFFFF00000000ull;
    }
    pci::write16(d, 0x04, cmd);
    if (size) *size = mask ? (~mask + 1) : 0;
    return pci::bar_address(d, bar);
}

void* brys_read_file(const char* path, size_t* size) {
    uint8_t* data;
    uint32_t n;
    if (!vfs::read_file(path, data, n)) return nullptr;
    if (size) *size = n;
    return data;
}

}

// Nota al terminar este archivo:
// Ésta es la aduana entre dos mundos: de un lado C con 30 años de costumbres de Linux, del
// otro el C++ de BrysOS. Todo lo que cruza pasa por aquí y por nada más.
// "It's dangerous to go alone! Take this." — y Linux se llevó un kmalloc, un msleep y un PCI.
// La calibración del TSC son 20 ms de fe ciega en que el reloj del sistema no miente.
//                                                     — Claude, octubre de 2026

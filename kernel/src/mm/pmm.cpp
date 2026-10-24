#include "mm/memory.h"
#include "lib/log.h"
#include "lib/string.h"
#include "arch/io.h"
#include "boot/bootinfo.h"

uint64_t mm::hhdm = HHDM_BASE;

namespace {
uint8_t* bitmap = nullptr;   // 1 = ocupado
uint64_t total_pages = 0;
uint64_t free_count = 0;
uint64_t usable_bytes = 0;
uint64_t search_hint = 0;

inline bool test(uint64_t i) { return bitmap[i / 8] & (1 << (i % 8)); }
inline void set(uint64_t i) { bitmap[i / 8] |= (1 << (i % 8)); }
inline void clear(uint64_t i) { bitmap[i / 8] &= ~(1 << (i % 8)); }

// Zonas que no deben entregarse aunque el mapa de memoria las marque como libres
struct Range { uint64_t start, end; };
Range reserved[BootInfo::MAX_MODULES + 8];
int nreserved = 0;

void reserve(uint64_t start, uint64_t end) {
    if (nreserved < (int)(sizeof reserved / sizeof reserved[0]))
        reserved[nreserved++] = {start & ~(PAGE_SIZE - 1), (end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1)};
}

// Devuelve el final de la zona reservada que contiene 'addr' (o 0 si no hay)
uint64_t reserved_end(uint64_t addr, uint64_t len) {
    for (int i = 0; i < nreserved; i++)
        if (addr < reserved[i].end && addr + len > reserved[i].start) return reserved[i].end;
    return 0;
}
}

void mm::pmm_init() {
    BootInfo& b = boot_info;
    if (!b.mem_count) panic("El cargador no proporcionó el mapa de memoria");
    reserve(0, 0x100000);                                             // BIOS, IVT, EBDA
    reserve((uint64_t)__kernel_phys_start, (uint64_t)__kernel_phys_end); // imagen del núcleo
    reserve(b.mbi_phys, b.mbi_phys + b.mbi_size);                       // información Multiboot2
    for (int i = 0; i < b.module_count; i++) reserve(b.modules[i].phys, b.modules[i].phys + b.modules[i].size);

    uint64_t top = 0;
    for (int i = 0; i < b.mem_count; i++) {
        const MemRegion& r = b.mem[i];
        if (r.type != 1) continue;
        usable_bytes += r.length;
        if (r.base + r.length > top) top = r.base + r.length;
    }
    total_pages = top / PAGE_SIZE;
    uint64_t bm_size = (total_pages + 7) / 8;

    // Coloca el mapa de bits por debajo de 4 GiB, fuera de las zonas reservadas
    for (int i = 0; i < b.mem_count && !bitmap; i++) {
        const MemRegion& r = b.mem[i];
        if (r.type != 1) continue;
        uint64_t s = (max<uint64_t>(r.base, 0x100000) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        uint64_t end = min<uint64_t>(r.base + r.length, 0x100000000ull);
        for (;;) {
            if (s + bm_size > end) break;
            uint64_t e = reserved_end(s, bm_size);
            if (!e) {
                bitmap = (uint8_t*)phys_to_virt(s);
                reserve(s, s + bm_size);
                break;
            }
            s = e;
        }
    }
    if (!bitmap) panic("Sin memoria para el mapa de bits");
    memset(bitmap, 0xFF, bm_size);

    for (int i = 0; i < b.mem_count; i++) {
        const MemRegion& r = b.mem[i];
        if (r.type != 1) continue;
        uint64_t s = (r.base + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        uint64_t e = (r.base + r.length) & ~(PAGE_SIZE - 1);
        for (uint64_t p = s; p < e; p += PAGE_SIZE) {
            if (reserved_end(p, PAGE_SIZE)) continue;
            if (test(p / PAGE_SIZE)) { clear(p / PAGE_SIZE); free_count++; }
        }
    }
    // Memoria por encima de 4 GiB: se añade al mapa directo con páginas de 2 MiB
    // (las tablas intermedias salen de páginas bajas, ya accesibles)
    for (int i = 0; i < b.mem_count; i++) {
        const MemRegion& r = b.mem[i];
        uint64_t end = r.base + r.length;
        if (r.type != 1 || end <= 0x100000000ull) continue;
        for (uint64_t a = max<uint64_t>(r.base, 0x100000000ull) & ~0x1FFFFFull; a < end; a += 0x200000) map_2m(hhdm + a, a);
    }
    klog("Memoria: %lu MiB utilizables, %lu MiB libres\n", usable_bytes >> 20, (free_count * PAGE_SIZE) >> 20);
}

uint64_t mm::alloc_pages(size_t count) {
    IrqGuard g;
    uint64_t run = 0;
    for (uint64_t pass = 0; pass < 2; pass++) {
        uint64_t start = pass == 0 ? search_hint : 0;
        run = 0;
        for (uint64_t i = start; i < total_pages; i++) {
            if (test(i)) { run = 0; continue; }
            if (++run == count) {
                uint64_t first = i + 1 - count;
                for (uint64_t j = first; j <= i; j++) set(j);
                free_count -= count;
                search_hint = i + 1;
                void* v = phys_to_virt(first * PAGE_SIZE);
                memset(v, 0, count * PAGE_SIZE);
                return first * PAGE_SIZE;
            }
        }
    }
    return 0;
}

uint64_t mm::alloc_page() { return alloc_pages(1); }

void mm::free_pages(uint64_t phys, size_t count) {
    IrqGuard g;
    for (size_t i = 0; i < count; i++) {
        uint64_t idx = phys / PAGE_SIZE + i;
        if (test(idx)) { clear(idx); free_count++; }
    }
    if (phys / PAGE_SIZE < search_hint) search_hint = phys / PAGE_SIZE;
}

uint64_t mm::total_memory() { return usable_bytes; }
uint64_t mm::free_memory() { return free_count * PAGE_SIZE; }

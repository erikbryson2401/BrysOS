#include "mm/memory.h"
#include "lib/log.h"
#include "arch/io.h"

namespace {
constexpr uint64_t ADDR_MASK = 0x000FFFFFFFFFF000ull;

uint64_t* next_level(uint64_t* table, uint64_t idx, bool create) {
    uint64_t e = table[idx];
    if (e & mm::PTE_PRESENT) {
        if (e & mm::PTE_HUGE) return nullptr;
        return (uint64_t*)mm::phys_to_virt(e & ADDR_MASK);
    }
    if (!create) return nullptr;
    uint64_t p = mm::alloc_page();
    if (!p) panic("Sin memoria para tablas de páginas");
    table[idx] = p | mm::PTE_PRESENT | mm::PTE_WRITE;
    return (uint64_t*)mm::phys_to_virt(p);
}

// ¿Está ya la dirección cubierta por una página grande?
bool covered_by_huge(uint64_t virt) {
    auto* pml4 = (uint64_t*)mm::phys_to_virt(read_cr3() & ADDR_MASK);
    uint64_t e = pml4[(virt >> 39) & 511];
    if (!(e & mm::PTE_PRESENT)) return false;
    auto* pdpt = (uint64_t*)mm::phys_to_virt(e & ADDR_MASK);
    e = pdpt[(virt >> 30) & 511];
    if (!(e & mm::PTE_PRESENT)) return false;
    if (e & mm::PTE_HUGE) return true;
    auto* pd = (uint64_t*)mm::phys_to_virt(e & ADDR_MASK);
    e = pd[(virt >> 21) & 511];
    return (e & mm::PTE_PRESENT) && (e & mm::PTE_HUGE);
}
}

void mm::vmm_map(uint64_t virt, uint64_t phys, uint64_t flags) {
    IrqGuard g;
    if (covered_by_huge(virt)) return;
    auto* pml4 = (uint64_t*)phys_to_virt(read_cr3() & ADDR_MASK);
    uint64_t* pdpt = next_level(pml4, (virt >> 39) & 511, true);
    uint64_t* pd = next_level(pdpt, (virt >> 30) & 511, true);
    uint64_t* pt = next_level(pd, (virt >> 21) & 511, true);
    if (!pt) return;
    pt[(virt >> 12) & 511] = (phys & ADDR_MASK) | flags | PTE_PRESENT;
    invlpg(virt);
}

uint64_t mm::vmm_translate(uint64_t virt) {
    auto* pml4 = (uint64_t*)phys_to_virt(read_cr3() & ADDR_MASK);
    uint64_t e = pml4[(virt >> 39) & 511];
    if (!(e & PTE_PRESENT)) return 0;
    auto* pdpt = (uint64_t*)phys_to_virt(e & ADDR_MASK);
    e = pdpt[(virt >> 30) & 511];
    if (!(e & PTE_PRESENT)) return 0;
    if (e & PTE_HUGE) return (e & 0x000FFFFFC0000000ull) + (virt & 0x3FFFFFFF);
    auto* pd = (uint64_t*)phys_to_virt(e & ADDR_MASK);
    e = pd[(virt >> 21) & 511];
    if (!(e & PTE_PRESENT)) return 0;
    if (e & PTE_HUGE) return (e & 0x000FFFFFFFE00000ull) + (virt & 0x1FFFFF);
    auto* pt = (uint64_t*)phys_to_virt(e & ADDR_MASK);
    e = pt[(virt >> 12) & 511];
    if (!(e & PTE_PRESENT)) return 0;
    return (e & ADDR_MASK) + (virt & 0xFFF);
}

// Ventana propia para E/S mapeada en memoria: páginas de 4 KiB sin caché
// (el mapa directo usa páginas de 2 MiB con caché y no sirve para MMIO)
static constexpr uint64_t MMIO_BASE = 0xFFFFE00000000000ull;

void* mm::map_mmio(uint64_t phys, size_t size) {
    uint64_t start = phys & ~(PAGE_SIZE - 1);
    uint64_t end = (phys + size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    for (uint64_t p = start; p < end; p += PAGE_SIZE)
        if (vmm_translate(MMIO_BASE + p) != p) vmm_map(MMIO_BASE + p, p, PTE_WRITE | PTE_PCD | PTE_PWT);
    return (void*)(phys + MMIO_BASE);
}

void mm::map_2m(uint64_t virt, uint64_t phys) {
    auto* pml4 = (uint64_t*)phys_to_virt(read_cr3() & ADDR_MASK);
    uint64_t* pdpt = next_level(pml4, (virt >> 39) & 511, true);
    if (!pdpt) return;
    uint64_t* pd = next_level(pdpt, (virt >> 30) & 511, true);
    if (!pd) return;
    uint64_t& e = pd[(virt >> 21) & 511];
    if (e & PTE_PRESENT) return;
    e = (phys & ~0x1FFFFFull) | PTE_PRESENT | PTE_WRITE | PTE_HUGE;
    invlpg(virt);
}

void mm::unmap_identity() {
    auto* pml4 = (uint64_t*)phys_to_virt(read_cr3() & ADDR_MASK);
    pml4[0] = 0;
    asm volatile("mov %%cr3, %%rax; mov %%rax, %%cr3" ::: "rax", "memory");
}

// Combinación de escritura para el framebuffer: la entrada 1 del PAT (PWT=1) pasa a WC
static bool pat_wc = false;

void mm::pat_init() {
    uint32_t a, b, c, d;
    asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1));
    if (!(d & (1 << 16))) return;
    uint32_t lo, hi;
    asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0x277));
    lo = (lo & ~0xFF00u) | (0x01u << 8);   // PA1 = WC
    asm volatile("wrmsr" ::"a"(lo), "d"(hi), "c"(0x277));
    asm volatile("mov %%cr3, %%rax; mov %%rax, %%cr3" ::: "rax", "memory");
    pat_wc = true;
}

void* mm::map_wc(uint64_t phys, size_t size) {
    if (!pat_wc) return map_mmio(phys, size);
    uint64_t start = phys & ~(PAGE_SIZE - 1);
    uint64_t end = (phys + size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    for (uint64_t p = start; p < end; p += PAGE_SIZE) vmm_map(MMIO_BASE + p, p, PTE_WRITE | PTE_PWT);
    return (void*)(phys + MMIO_BASE);
}

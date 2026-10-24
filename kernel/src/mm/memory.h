#pragma once
#include <stddef.h>
#include <stdint.h>

constexpr uint64_t PAGE_SIZE = 4096;

namespace mm {
extern uint64_t hhdm;

inline void* phys_to_virt(uint64_t p) { return (void*)(p + hhdm); }
inline uint64_t virt_to_phys_hhdm(const void* v) { return (uint64_t)v - hhdm; }

// Asignador físico de páginas (mapa de bits)
void pmm_init();
uint64_t alloc_page();                  // devuelve dirección física (0 si no hay)
uint64_t alloc_pages(size_t count);     // contiguas
void free_pages(uint64_t phys, size_t count);
uint64_t total_memory();
uint64_t free_memory();

// Paginación
constexpr uint64_t PTE_PRESENT = 1, PTE_WRITE = 2, PTE_USER = 4, PTE_PWT = 8, PTE_PCD = 16,
                   PTE_HUGE = 128, PTE_NX = 1ull << 63;
void vmm_map(uint64_t virt, uint64_t phys, uint64_t flags);
void map_2m(uint64_t virt, uint64_t phys);   // página grande de 2 MiB, escritura
void unmap_identity();                        // quita la identidad usada en el arranque
uint64_t vmm_translate(uint64_t virt);
// Mapea una región MMIO dentro del HHDM sin caché y devuelve su dirección virtual
void* map_mmio(uint64_t phys, size_t size);
// Mapea memoria de vídeo con combinación de escritura (si el procesador tiene PAT)
void pat_init();
void* map_wc(uint64_t phys, size_t size);

// Montón del kernel
void heap_init();
void* kmalloc(size_t size);
void* kcalloc(size_t size);
void* krealloc(void* p, size_t size);
void kfree(void* p);
uint64_t heap_used();
}

void* operator new(size_t sz);
void* operator new[](size_t sz);
void operator delete(void* p) noexcept;
void operator delete[](void* p) noexcept;
void operator delete(void* p, size_t) noexcept;
void operator delete[](void* p, size_t) noexcept;
inline void* operator new(size_t, void* p) noexcept { return p; }

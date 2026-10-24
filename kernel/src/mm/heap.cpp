// Montón del kernel: lista libre ordenada por dirección con fusión de bloques.
// Crece mapeando páginas físicas en una región virtual dedicada.
#include "mm/memory.h"
#include "lib/log.h"
#include "lib/string.h"
#include "arch/io.h"

namespace {
constexpr uint64_t HEAP_BASE = 0xFFFFC00000000000ull;
constexpr uint64_t HEAP_MAX = 1024ull << 20 << 2; // 4 GiB virtuales
constexpr uint64_t MAGIC = 0xB4A5B4A5C0FFEE00ull;

struct Block {
    uint64_t size;   // tamaño total incluyendo cabecera
    uint64_t magic;  // MAGIC | 1 si está en uso
    Block* next;     // sólo válido si está libre
    uint64_t pad;
};
static_assert(sizeof(Block) == 32);

Block* free_list = nullptr;
uint64_t heap_top = HEAP_BASE;
uint64_t used_bytes = 0;

void insert_free(Block* b) {
    b->magic = MAGIC;
    Block** pp = &free_list;
    while (*pp && *pp < b) pp = &(*pp)->next;
    b->next = *pp;
    *pp = b;
    // fusiona con el siguiente
    if (b->next && (uint8_t*)b + b->size == (uint8_t*)b->next) {
        b->size += b->next->size;
        b->next = b->next->next;
    }
    // fusiona con el anterior
    if (pp != &free_list) {
        Block* prev = (Block*)((uint8_t*)pp - __builtin_offsetof(Block, next));
        if ((uint8_t*)prev + prev->size == (uint8_t*)b) {
            prev->size += b->size;
            prev->next = b->next;
        }
    }
}

bool grow(uint64_t bytes) {
    bytes = (bytes + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    if (bytes < 256 * 1024) bytes = 256 * 1024;
    if (heap_top + bytes > HEAP_BASE + HEAP_MAX) return false;
    uint64_t start = heap_top;
    for (uint64_t off = 0; off < bytes; off += PAGE_SIZE) {
        uint64_t p = mm::alloc_page();
        if (!p) return false;
        mm::vmm_map(heap_top, p, mm::PTE_WRITE);
        heap_top += PAGE_SIZE;
    }
    Block* b = (Block*)start;
    b->size = bytes;
    insert_free(b);
    return true;
}
}

void mm::heap_init() {
    grow(1 << 20);
    klog("Montón del kernel listo en %p\n", (void*)HEAP_BASE);
}

void* mm::kmalloc(size_t size) {
    if (!size) size = 1;
    uint64_t need = ((size + 15) & ~15ull) + sizeof(Block);
    IrqGuard g;
    for (int attempt = 0; attempt < 2; attempt++) {
        Block** pp = &free_list;
        while (*pp) {
            Block* b = *pp;
            if (b->size >= need) {
                if (b->size - need >= sizeof(Block) + 32) {
                    Block* rest = (Block*)((uint8_t*)b + need);
                    rest->size = b->size - need;
                    rest->magic = MAGIC;
                    rest->next = b->next;
                    *pp = rest;
                    b->size = need;
                } else {
                    *pp = b->next;
                }
                b->magic = MAGIC | 1;
                used_bytes += b->size;
                return (uint8_t*)b + sizeof(Block);
            }
            pp = &b->next;
        }
        if (!grow(need)) break;
    }
    klog("kmalloc: sin memoria (%lu bytes)\n", (uint64_t)size);
    return nullptr;
}

void* mm::kcalloc(size_t size) {
    void* p = kmalloc(size);
    if (p) memset(p, 0, size);
    return p;
}

void mm::kfree(void* p) {
    if (!p) return;
    Block* b = (Block*)((uint8_t*)p - sizeof(Block));
    if (b->magic != (MAGIC | 1)) panic("kfree: bloque corrupto o liberado dos veces en %p", p);
    IrqGuard g;
    used_bytes -= b->size;
    insert_free(b);
}

void* mm::krealloc(void* p, size_t size) {
    if (!p) return kmalloc(size);
    Block* b = (Block*)((uint8_t*)p - sizeof(Block));
    size_t old = b->size - sizeof(Block);
    if (old >= size) return p;
    void* n = kmalloc(size);
    if (!n) return nullptr;
    memcpy(n, p, old);
    kfree(p);
    return n;
}

uint64_t mm::heap_used() { return used_bytes; }

void* operator new(size_t sz) { return mm::kcalloc(sz); }
void* operator new[](size_t sz) { return mm::kcalloc(sz); }
void operator delete(void* p) noexcept { mm::kfree(p); }
void operator delete[](void* p) noexcept { mm::kfree(p); }
void operator delete(void* p, size_t) noexcept { mm::kfree(p); }
void operator delete[](void* p, size_t) noexcept { mm::kfree(p); }

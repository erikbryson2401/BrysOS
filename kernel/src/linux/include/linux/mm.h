/* BrysOS: páginas de memoria física (struct page). En BrysOS cada struct page es un pequeño
 * descriptor aparte que apunta a su página (brys/netdev.c). */
#ifndef _BRYS_LINUX_MM_H
#define _BRYS_LINUX_MM_H

#include <linux/kernel.h>
#include <linux/gfp.h>
#include <linux/spinlock.h>

struct device;

struct page {
    void* brys_addr;        /* dirección virtual (mapa directo) */
    unsigned int brys_order;
    int brys_refs;
};

struct page* alloc_pages(gfp_t gfp, unsigned int order);
#define alloc_page(gfp) alloc_pages(gfp, 0)
#define dev_alloc_page() alloc_pages(GFP_ATOMIC, 0)
#define dev_alloc_pages(order) alloc_pages(GFP_ATOMIC, order)
void __free_pages(struct page* page, unsigned int order);
#define __free_page(p) __free_pages(p, 0)
static inline void get_page(struct page* p) { p->brys_refs++; }
void put_page(struct page* p);
static inline void* page_address(const struct page* p) { return p->brys_addr; }
#define lowmem_page_address page_address
struct page* virt_to_page(const void* addr);
static inline int page_count(const struct page* p) { return p->brys_refs; }
static inline int page_to_nid(const struct page* p) { (void)p; return 0; }
static inline bool page_is_pfmemalloc(const struct page* p) { (void)p; return false; }
#define dev_page_is_reusable(p) true
phys_addr_t page_to_phys(const struct page* p);
dma_addr_t dma_map_page(struct device* dev, struct page* page, size_t off, size_t size, int dir);
static inline void dma_unmap_page(struct device* dev, dma_addr_t a, size_t s, int dir) {
    (void)dev; (void)a; (void)s; (void)dir;
}
#define kmap(p) page_address(p)
#define kunmap(p) do { } while (0)
#define kmap_atomic(p) page_address(p)
#define kunmap_atomic(a) do { } while (0)
#define kmap_local_page(p) page_address(p)
#define kunmap_local(a) do { } while (0)
#define offset_in_page(p) ((unsigned long)(p) & ~PAGE_MASK)

struct dma_pool;

#endif

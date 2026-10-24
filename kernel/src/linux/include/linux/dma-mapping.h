/* BrysOS: memoria para DMA. Las páginas están por debajo de 4 GiB y el mapa directo de
 * BrysOS da la dirección física, así que "mapear" es traducir la dirección. */
#ifndef _BRYS_LINUX_DMA_MAPPING_H
#define _BRYS_LINUX_DMA_MAPPING_H

#include <linux/types.h>
#include <linux/gfp.h>

struct device;
enum dma_data_direction { DMA_BIDIRECTIONAL = 0, DMA_TO_DEVICE = 1, DMA_FROM_DEVICE = 2, DMA_NONE = 3 };
#define DMA_BIT_MASK(n) (((n) == 64) ? ~0ULL : ((1ULL << (n)) - 1))
#define DMA_MAPPING_ERROR (~(dma_addr_t)0)

void* dma_alloc_coherent(struct device* dev, size_t size, dma_addr_t* handle, gfp_t gfp);
void dma_free_coherent(struct device* dev, size_t size, void* cpu, dma_addr_t handle);
#define dma_zalloc_coherent dma_alloc_coherent
dma_addr_t dma_map_single(struct device* dev, void* ptr, size_t size, enum dma_data_direction dir);
static inline void dma_unmap_single(struct device* dev, dma_addr_t a, size_t s, enum dma_data_direction d) {
    (void)dev; (void)a; (void)s; (void)d;
}
static inline int dma_mapping_error(struct device* dev, dma_addr_t a) { (void)dev; return a == DMA_MAPPING_ERROR; }
static inline void dma_sync_single_for_cpu(struct device* dev, dma_addr_t a, size_t s, enum dma_data_direction d) {
    (void)dev; (void)a; (void)s; (void)d;
}
static inline void dma_sync_single_for_device(struct device* dev, dma_addr_t a, size_t s, enum dma_data_direction d) {
    (void)dev; (void)a; (void)s; (void)d;
}
int dma_set_mask(struct device* dev, u64 mask);
int dma_set_coherent_mask(struct device* dev, u64 mask);
int dma_set_mask_and_coherent(struct device* dev, u64 mask);

#endif

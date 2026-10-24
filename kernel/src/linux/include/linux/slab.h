/* BrysOS: kmalloc y compañía sobre el montón de BrysOS (los GFP_* no cambian nada) */
#ifndef _BRYS_LINUX_SLAB_H
#define _BRYS_LINUX_SLAB_H

#include <linux/kernel.h>
#include <linux/gfp.h>

void* kmalloc(size_t size, gfp_t flags);
void* kzalloc(size_t size, gfp_t flags);
void* krealloc(const void* p, size_t size, gfp_t flags);
void kfree(const void* p);
#define kfree_sensitive kfree
#define kvfree kfree
#define vfree kfree
static inline void* kcalloc(size_t n, size_t size, gfp_t flags) {
    if (size && n > (size_t)-1 / size) return NULL;
    return kzalloc(n * size, flags);
}
static inline void* kmalloc_array(size_t n, size_t size, gfp_t flags) {
    if (size && n > (size_t)-1 / size) return NULL;
    return kmalloc(n * size, flags);
}
#define krealloc_array(p, n, size, flags) krealloc(p, (n) * (size), flags)
#define kvmalloc kmalloc
#define kvzalloc kzalloc
#define kvcalloc kcalloc
#define kvmalloc_array kmalloc_array
#define vmalloc(size) kmalloc(size, GFP_KERNEL)
#define vzalloc(size) kzalloc(size, GFP_KERNEL)
#define kzalloc_node(size, flags, node) kzalloc(size, flags)
#define kmalloc_node(size, flags, node) kmalloc(size, flags)
#define struct_size(p, member, count) (sizeof(*(p)) + sizeof(*(p)->member) * (count))
#define flex_array_size(p, member, count) (sizeof(*(p)->member) * (count))
#define array_size(a, b) ((a) * (b))
size_t ksize(const void* p);

struct kmem_cache {
    size_t size;
    const char* name;
};
struct kmem_cache* kmem_cache_create(const char* name, unsigned int size, unsigned int align,
                                     unsigned long flags, void (*ctor)(void*));
void kmem_cache_destroy(struct kmem_cache* c);
void* kmem_cache_alloc(struct kmem_cache* c, gfp_t flags);
void* kmem_cache_zalloc(struct kmem_cache* c, gfp_t flags);
void kmem_cache_free(struct kmem_cache* c, void* p);
#define SLAB_HWCACHE_ALIGN 0

#endif

/* BrysOS: tipos básicos del núcleo Linux */
#ifndef _BRYS_LINUX_TYPES_H
#define _BRYS_LINUX_TYPES_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>

typedef uint8_t u8, __u8;
typedef uint16_t u16, __u16;
typedef uint32_t u32, __u32;
typedef unsigned long long u64, __u64;
typedef int8_t s8, __s8;
typedef int16_t s16, __s16;
typedef int32_t s32, __s32;
typedef long long s64, __s64;

typedef u16 __le16, __be16;
typedef u32 __le32, __be32;
typedef u64 __le64, __be64;
typedef u16 __sum16;
typedef u32 __wsum;

typedef long ssize_t;
typedef long long loff_t;
typedef u64 phys_addr_t;
typedef u64 resource_size_t;
typedef u64 dma_addr_t;
typedef unsigned int gfp_t;
typedef unsigned int fmode_t;
typedef int pid_t;
typedef unsigned short umode_t;
typedef unsigned long uintptr_t_linux;
typedef struct { int counter; } atomic_t;
typedef struct { long counter; } atomic64_t;
typedef atomic_t refcount_t;

struct list_head {
    struct list_head *next, *prev;
};
struct hlist_head {
    struct hlist_node* first;
};
struct hlist_node {
    struct hlist_node *next, **pprev;
};

#define __bitwise
#define __force
#define __user
#define __iomem
#define __rcu
#define __percpu
#define __must_check __attribute__((warn_unused_result))

#endif

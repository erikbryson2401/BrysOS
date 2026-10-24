/* BrysOS: precarga en la caché */
#ifndef _BRYS_LINUX_PREFETCH_H
#define _BRYS_LINUX_PREFETCH_H
#define prefetch(x) __builtin_prefetch(x)
#define prefetchw(x) __builtin_prefetch(x, 1)
#define net_prefetch(x) prefetch(x)
#define net_prefetchw(x) prefetchw(x)
#endif

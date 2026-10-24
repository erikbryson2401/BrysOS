/* BrysOS: indicadores de reserva de memoria (se aceptan y se ignoran) */
#ifndef _BRYS_LINUX_GFP_H
#define _BRYS_LINUX_GFP_H

#include <linux/types.h>

#define GFP_KERNEL 0x01u
#define GFP_ATOMIC 0x02u
#define GFP_NOWAIT 0x04u
#define GFP_DMA 0x08u
#define GFP_DMA32 0x10u
#define GFP_NOIO 0x20u
#define GFP_NOFS 0x40u
#define GFP_USER GFP_KERNEL
#define __GFP_ZERO 0x100u
#define __GFP_NOWARN 0x200u
#define __GFP_COMP 0x400u
#define __GFP_RETRY_MAYFAIL 0x800u
#define __GFP_DMA GFP_DMA

unsigned long __get_free_pages(gfp_t gfp, unsigned int order);
void free_pages(unsigned long addr, unsigned int order);
#define __get_free_page(gfp) __get_free_pages(gfp, 0)
#define get_zeroed_page(gfp) __get_free_pages((gfp) | __GFP_ZERO, 0)
#define free_page(addr) free_pages(addr, 0)
#define PAGE_SIZE 4096UL
#define PAGE_SHIFT 12
#define PAGE_MASK (~(PAGE_SIZE - 1))

#endif

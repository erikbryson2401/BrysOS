/* BrysOS: acceso a registros mapeados en memoria (MMIO) y puertos de E/S */
#ifndef _BRYS_LINUX_IO_H
#define _BRYS_LINUX_IO_H

#include <linux/types.h>
#include <linux/compiler.h>

static inline u8 readb(const volatile void* a) { u8 v = *(const volatile u8*)a; barrier(); return v; }
static inline u16 readw(const volatile void* a) { u16 v = *(const volatile u16*)a; barrier(); return v; }
static inline u32 readl(const volatile void* a) { u32 v = *(const volatile u32*)a; barrier(); return v; }
static inline u64 readq(const volatile void* a) { u64 v = *(const volatile u64*)a; barrier(); return v; }
static inline void writeb(u8 v, volatile void* a) { barrier(); *(volatile u8*)a = v; }
static inline void writew(u16 v, volatile void* a) { barrier(); *(volatile u16*)a = v; }
static inline void writel(u32 v, volatile void* a) { barrier(); *(volatile u32*)a = v; }
static inline void writeq(u64 v, volatile void* a) { barrier(); *(volatile u64*)a = v; }
#define readb_relaxed readb
#define readw_relaxed readw
#define readl_relaxed readl
#define writeb_relaxed writeb
#define writew_relaxed writew
#define writel_relaxed writel
#define ioread8 readb
#define ioread16 readw
#define ioread32 readl
#define iowrite8 writeb
#define iowrite16 writew
#define iowrite32 writel
#define __raw_readl readl
#define __raw_writel writel
#define mmiowb() barrier()

static inline u8 inb(u16 port) { u8 v; __asm__ __volatile__("inb %1, %0" : "=a"(v) : "Nd"(port)); return v; }
static inline u16 inw(u16 port) { u16 v; __asm__ __volatile__("inw %1, %0" : "=a"(v) : "Nd"(port)); return v; }
static inline u32 inl(u16 port) { u32 v; __asm__ __volatile__("inl %1, %0" : "=a"(v) : "Nd"(port)); return v; }
static inline void outb(u8 v, u16 port) { __asm__ __volatile__("outb %0, %1" ::"a"(v), "Nd"(port)); }
static inline void outw(u16 v, u16 port) { __asm__ __volatile__("outw %0, %1" ::"a"(v), "Nd"(port)); }
static inline void outl(u32 v, u16 port) { __asm__ __volatile__("outl %0, %1" ::"a"(v), "Nd"(port)); }

void* ioremap(phys_addr_t phys, size_t size);
#define ioremap_nocache ioremap
#define ioremap_uc ioremap
static inline void iounmap(volatile void* addr) { (void)addr; }   /* el mapeo se conserva */

phys_addr_t virt_to_phys(const volatile void* addr);
void* phys_to_virt(phys_addr_t addr);
#define __pa(x) virt_to_phys((void*)(x))
#define __va(x) phys_to_virt((phys_addr_t)(x))

#endif

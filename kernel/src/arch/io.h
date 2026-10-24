#pragma once
#include <stdint.h>

static inline void outb(uint16_t port, uint8_t v) { asm volatile("outb %0, %1" ::"a"(v), "Nd"(port)); }
static inline void outw(uint16_t port, uint16_t v) { asm volatile("outw %0, %1" ::"a"(v), "Nd"(port)); }
static inline void outl(uint16_t port, uint32_t v) { asm volatile("outl %0, %1" ::"a"(v), "Nd"(port)); }
static inline uint8_t inb(uint16_t port) { uint8_t r; asm volatile("inb %1, %0" : "=a"(r) : "Nd"(port)); return r; }
static inline uint16_t inw(uint16_t port) { uint16_t r; asm volatile("inw %1, %0" : "=a"(r) : "Nd"(port)); return r; }
static inline uint32_t inl(uint16_t port) { uint32_t r; asm volatile("inl %1, %0" : "=a"(r) : "Nd"(port)); return r; }
static inline void io_wait() { outb(0x80, 0); }

static inline void cli() { asm volatile("cli" ::: "memory"); }
static inline void sti() { asm volatile("sti" ::: "memory"); }
static inline void hlt() { asm volatile("hlt" ::: "memory"); }

static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}
static inline void wrmsr(uint32_t msr, uint64_t v) {
    asm volatile("wrmsr" ::"a"((uint32_t)v), "d"((uint32_t)(v >> 32)), "c"(msr));
}
constexpr uint32_t MSR_EFER = 0xC0000080, MSR_STAR = 0xC0000081, MSR_LSTAR = 0xC0000082,
                   MSR_FMASK = 0xC0000084, MSR_FS_BASE = 0xC0000100;

static inline uint64_t read_cr2() { uint64_t v; asm volatile("mov %%cr2, %0" : "=r"(v)); return v; }
static inline uint64_t read_cr3() { uint64_t v; asm volatile("mov %%cr3, %0" : "=r"(v)); return v; }
static inline void invlpg(uint64_t addr) { asm volatile("invlpg (%0)" ::"r"(addr) : "memory"); }

static inline uint64_t save_irq() {
    uint64_t f;
    asm volatile("pushfq; pop %0; cli" : "=r"(f)::"memory");
    return f;
}
static inline void restore_irq(uint64_t f) {
    if (f & 0x200) sti();
}

// Bloqueo de interrupciones con RAII
struct IrqGuard {
    uint64_t flags;
    IrqGuard() : flags(save_irq()) {}
    ~IrqGuard() { restore_irq(flags); }
};

[[noreturn]] static inline void halt_forever() {
    for (;;) { cli(); hlt(); }
}

#include "arch/gdt.h"
#include "lib/string.h"

namespace {

struct [[gnu::packed]] Tss {
    uint32_t rsv0;
    uint64_t rsp[3];
    uint64_t rsv1;
    uint64_t ist[7];
    uint64_t rsv2;
    uint16_t rsv3;
    uint16_t iomap;
};

struct [[gnu::packed]] Gdtr {
    uint16_t limit;
    uint64_t base;
};

alignas(16) uint64_t table[7];
Tss tss;
alignas(16) uint8_t df_stack[16384];   // pila para doble fallo (IST1)
alignas(16) uint8_t nmi_stack[8192];   // pila para NMI / machine check (IST2)

}

void gdt::init() {
    table[0] = 0;
    table[1] = 0x00AF9A000000FFFF; // código kernel 64 bits
    table[2] = 0x00CF92000000FFFF; // datos kernel
    table[3] = 0x00AFFA000000FFFF; // código usuario
    table[4] = 0x00CFF2000000FFFF; // datos usuario

    memset(&tss, 0, sizeof tss);
    tss.ist[0] = (uint64_t)(df_stack + sizeof df_stack);
    tss.ist[1] = (uint64_t)(nmi_stack + sizeof nmi_stack);
    tss.iomap = sizeof(Tss);

    uint64_t base = (uint64_t)&tss;
    uint64_t limit = sizeof(Tss) - 1;
    table[5] = (limit & 0xFFFF) | ((base & 0xFFFFFF) << 16) | (0x89ull << 40) |
               (((limit >> 16) & 0xF) << 48) | (((base >> 24) & 0xFF) << 56);
    table[6] = base >> 32;

    Gdtr gdtr{sizeof(table) - 1, (uint64_t)table};
    asm volatile(
        "lgdt %0\n"
        "pushq $0x08\n"
        "leaq 1f(%%rip), %%rax\n"
        "pushq %%rax\n"
        "lretq\n"
        "1:\n"
        "movw $0x10, %%ax\n"
        "movw %%ax, %%ds\n"
        "movw %%ax, %%es\n"
        "movw %%ax, %%ss\n"
        "xorw %%ax, %%ax\n"
        "movw %%ax, %%fs\n"
        "movw %%ax, %%gs\n"
        "movw $0x28, %%ax\n"
        "ltr %%ax\n"
        :: "m"(gdtr) : "rax", "memory");
}

void gdt::set_kernel_stack(uint64_t top) { tss.rsp[0] = top; }

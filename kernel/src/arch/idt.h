#pragma once
#include <stdint.h>

struct Frame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector, error;
    uint64_t rip, cs, rflags, rsp, ss;
};

using IrqHandler = void (*)(Frame*);

namespace idt {
void init();
// Registra un manejador para la IRQ 0-15 del PIC (vectores 32-47)
void set_irq_handler(int irq, IrqHandler h);
// Manejadores adicionales de una línea compartida (PCI): se llaman todos en cada
// interrupción, hasta 4 por línea. Devuelve false si no queda sitio.
using SharedIrqHandler = void (*)(void* ctx);
bool add_irq_handler(int irq, SharedIrqHandler h, void* ctx);
void remove_irq_handler(int irq, SharedIrqHandler h, void* ctx);
}

namespace pic {
void init();
void mask(int irq);
void unmask(int irq);
void eoi(int irq);
}

namespace pit {
void init(uint32_t hz);
uint64_t ticks();       // milisegundos desde el arranque
void sleep_ms(uint64_t ms);
// Función que se llama en cada interrupción del reloj (con las interrupciones desactivadas)
void set_hook(void (*fn)());
}

#pragma once
#include <stdint.h>

namespace gdt {
constexpr uint16_t KCODE = 0x08;
constexpr uint16_t KDATA = 0x10;
constexpr uint16_t UCODE = 0x18;   // programas .run (anillo 3)
constexpr uint16_t UDATA = 0x20;
void init();
// Pila de núcleo que usa el procesador al pasar de un programa al núcleo (TSS.RSP0)
void set_kernel_stack(uint64_t top);
}

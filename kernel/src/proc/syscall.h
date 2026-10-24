#pragma once
#include "arch/idt.h"

namespace syscall {
// int 0x80 desde un programa .run: número en rax, argumentos en rdi, rsi, rdx, r10, r8, r9
void dispatch(Frame* f);
// Una llamada de la ABI de los .run (SYS_*) desde otra entrada (los programas de Linux la
// usan con el número 0x4000 + n). Se ejecuta con las interrupciones activas.
int64_t call(uint64_t n, uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e);
}

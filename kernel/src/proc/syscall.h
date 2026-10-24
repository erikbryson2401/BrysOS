#pragma once
#include "arch/idt.h"

namespace syscall {
// int 0x80 desde un programa .run: número en rax, argumentos en rdi, rsi, rdx, r10, r8, r9
void dispatch(Frame* f);
}

#pragma once
// ABI de Linux x86-64 para los programas: los ejecutables estáticos de Linux (por ejemplo,
// compilados con musl) entran al núcleo con la instrucción 'syscall' y se atienden aquí con
// los números y las estructuras de Linux. Los .run siguen usando 'int 0x80' (proc/syscall).
#include "arch/idt.h"
#include <stdint.h>

struct Process;

namespace linux_abi {
struct State;
void init();                                  // activa la instrucción 'syscall' (MSR)
State* create_state(Process* p);              // descriptores 0, 1 y 2 y carpeta actual
void destroy_state(Process* p);               // cierra los archivos (guarda los cambios)
bool build_stack(Process* p, uint64_t& sp);   // argc, argv, envp y vector auxiliar
void dispatch(Frame* f);                      // una llamada (vector 0x82)

// Rutas: "/c/carpeta/archivo" <-> "C:\carpeta\archivo"
bool to_brys_path(const char* linux_path, const char* cwd, char* out, uint64_t cap);
void to_linux_path(const char* brys_path, char* out, uint64_t cap);
}

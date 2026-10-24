#pragma once
// Programas .run: procesos con su propio espacio de direcciones en modo usuario
#include "lib/vec.h"
#include "proc/sched.h"
#include "../../../sdk/include/brysos_abi.h"

class Window;
class Widget;
namespace linux_abi { struct State; }

struct Process {
    int pid = 0;
    String name;                     // "Serpiente"
    String path;                     // C:\Archivos de programa\Serpiente.run
    String args;
    uint64_t pml4_phys = 0;
    uint64_t* pml4 = nullptr;
    uint64_t brk_base = 0, brk = 0;  // montón (crece hacia arriba)
    uint64_t canvas_next = 0x100000000ull;   // dónde se mapean los lienzos
    sched::Thread* thread = nullptr;
    Vec<Window*> windows;            // identificador = índice + 1 (nullptr si se cerró)
    Vec<Widget*> controls;           // identificador = índice + 1
    Vec<int> control_types;          // CONTROL_BOTON, CONTROL_ETIQUETA...
    Vec<uint64_t> canvases;          // memoria física de los lienzos (para liberarla)
    Vec<uint32_t> canvas_pages;
    // eventos pendientes (cola circular)
    static constexpr int QUEUE = 256;
    Evento queue[QUEUE];
    int qhead = 0, qtail = 0;
    uint64_t waiting_since = 0;      // instante del evento más antiguo sin leer (0 = ninguno)
    // mensaje en curso
    volatile int msg_result = -1;
    Window* msg_window = nullptr;
    // fin
    bool dying = false;
    bool crashed = false;
    String crash_info;
    int exit_code = 0;
    uint64_t started = 0;
    // Programas de Linux (llamadas 'syscall'): descriptores de archivo, carpeta actual, mmap
    linux_abi::State* lx = nullptr;
    uint64_t phdr = 0, phnum = 0, entry = 0;   // para el vector auxiliar (AT_PHDR...)
};

namespace process {
// Carga y arranca un .run. Devuelve nullptr y el motivo en 'err' si no se puede.
Process* start(const char* path, const char* args, String& err);
[[noreturn]] void exit_current(int code);          // llamada SALIR del propio programa
Frame* fault(Frame* f, const char* what);           // excepción en modo usuario
void kill(Process* p);                              // "Finalizar tarea"
void reap();                                        // hilo 0: libera los que han terminado
const Vec<Process*>& list();
Process* current();
void sync_kernel_half(Process* p);

// Memoria del programa
bool map_page(Process* p, uint64_t virt, uint64_t phys, bool write, bool owned);
bool alloc_range(Process* p, uint64_t virt, uint64_t bytes);   // páginas nuevas a cero
bool user_ok(Process* p, uint64_t addr, uint64_t len, bool write);
bool user_string(Process* p, uint64_t addr, String& out, size_t max = 4096);
// Libera las páginas propias de un rango (munmap)
void free_range(Process* p, uint64_t virt, uint64_t bytes);
// Copia datos al espacio del programa aunque no sea el activo (pila inicial)
bool poke(Process* p, uint64_t virt, const void* data, uint64_t len);

// Eventos
void push_event(Process* p, const Evento& e);
bool pop_event(Process* p, Evento& e);
bool responding(Process* p);   // false si hace más de 5 s que no lee sus eventos

// Programas incluidos con el sistema (en C:\Archivos de programa)
const char* programs_dir();
void install_bundled();
}

#pragma once
// Planificador: el hilo 0 es el propio núcleo (interfaz gráfica, controladores) y los demás,
// los programas .run.
//
// Reglas (un solo procesador):
//  - El código del núcleo nunca se interrumpe para pasar a otro hilo: sólo cambia de hilo
//    cuando el hilo actual estaba ejecutando su programa (modo usuario) al llegar la
//    interrupción del reloj, o cuando lo pide él mismo en un punto seguro (yield).
//  - El hilo 0 cede el procesador cuando no tiene trabajo (en el bucle de la interfaz).
//  - Una llamada al sistema se ejecuta entera; si tiene que esperar, cede con yield().
// Así no hacen falta cerrojos: en cada momento sólo un hilo está dentro del núcleo fuera
// de un punto seguro.
#include <stdint.h>
#include "arch/idt.h"

struct Process;

namespace sched {
enum class State { READY, BLOCKED, ZOMBIE };

struct Thread {
    int id = 0;
    Frame* frame = nullptr;          // contexto guardado (en su pila de núcleo)
    uint8_t* kstack = nullptr;
    uint64_t kstack_top = 0;
    uint64_t cr3 = 0;                // tabla de páginas (0 = la del núcleo)
    State state = State::READY;
    uint64_t wake_at = 0;            // bloqueado hasta este instante (0 = sin plazo)
    bool (*wait_cond)(void*) = nullptr;   // ...o hasta que esto devuelva true
    void* wait_ctx = nullptr;
    Process* proc = nullptr;
    uint64_t cpu_ms = 0;             // tiempo de procesador aproximado
    alignas(16) uint8_t fpu[512];    // estado x87/SSE (FXSAVE)
};

void init();                         // crea el hilo 0 y activa SSE
Thread* current();
Thread* create_user(Process* p, uint64_t entry, uint64_t user_stack, uint64_t arg);
void destroy(Thread* t);             // libera un hilo que no se está ejecutando
void yield();                        // cede el procesador (punto seguro)
// Bloquea el hilo actual hasta que pase 'ms' o se cumpla la condición
void sleep(uint32_t ms);
void wait(bool (*cond)(void*), void* ctx);
void idle();                         // hilo 0 sin trabajo: cede a los programas o espera
bool others_ready();

// Desde la entrada de interrupciones (arch/idt.cpp)
Frame* schedule(Frame* f);           // guarda 'f' y devuelve el contexto del siguiente hilo
Frame* preempt(Frame* f, bool to_kernel);   // tras una IRQ que interrumpió a un programa
}

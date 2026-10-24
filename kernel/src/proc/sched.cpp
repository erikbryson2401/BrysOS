#include "proc/sched.h"
#include "proc/process.h"
#include "arch/gdt.h"
#include "arch/io.h"
#include "mm/memory.h"
#include "lib/log.h"
#include "lib/string.h"
#include "lib/vec.h"

namespace {
constexpr uint64_t KSTACK = 32 * 1024;
constexpr uint64_t QUANTUM_MS = 10;

Vec<sched::Thread*> threads;
sched::Thread* cur = nullptr;
sched::Thread kernel_thread;     // hilo 0
uint64_t kernel_cr3 = 0;
int next_id = 1;
uint64_t slice_start = 0;
alignas(16) uint8_t fpu_init_state[512];

void fxsave(uint8_t* a) { asm volatile("fxsave64 (%0)" ::"r"(a) : "memory"); }
void fxrstor(const uint8_t* a) { asm volatile("fxrstor64 (%0)" ::"r"(a) : "memory"); }

bool runnable(sched::Thread* t) {
    if (t->state == sched::State::READY) return true;
    if (t->state != sched::State::BLOCKED) return false;
    if (t->wake_at && pit::ticks() >= t->wake_at) return true;
    if (t->wait_cond && t->wait_cond(t->wait_ctx)) return true;
    return false;
}

void switch_to(sched::Thread* next) {
    if (next == cur) return;
    fxsave(cur->fpu);
    fxrstor(next->fpu);
    gdt::set_kernel_stack(next->kstack_top);
    uint64_t want = next->cr3 ? next->cr3 : kernel_cr3;
    if (next->proc) process::sync_kernel_half(next->proc);
    if (read_cr3() != want) asm volatile("mov %0, %%cr3" ::"r"(want) : "memory");
    uint64_t now = pit::ticks();
    cur->cpu_ms += now - slice_start;
    slice_start = now;
    cur = next;
}

// Siguiente hilo ejecutable, por turnos, empezando detrás del actual
sched::Thread* pick(bool prefer_kernel) {
    if (prefer_kernel && cur != &kernel_thread) return &kernel_thread;
    size_t n = threads.size();
    size_t start = 0;
    for (size_t i = 0; i < n; i++)
        if (threads[i] == cur) start = i + 1;
    for (size_t k = 0; k < n; k++) {
        sched::Thread* t = threads[(start + k) % n];
        if (t != cur && runnable(t)) return t;
    }
    if (runnable(cur)) return cur;
    return &kernel_thread;   // el núcleo siempre puede continuar
}
}

void sched::init() {
    // SSE: CR0.EM = 0, CR0.MP = 1, CR4.OSFXSR y CR4.OSXMMEXCPT
    uint64_t cr0, cr4;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 = (cr0 & ~(1ull << 2)) | (1ull << 1);
    asm volatile("mov %0, %%cr0" ::"r"(cr0));
    asm volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1ull << 9) | (1ull << 10);
    asm volatile("mov %0, %%cr4" ::"r"(cr4));
    asm volatile("fninit");
    uint32_t mxcsr = 0x1F80;
    asm volatile("ldmxcsr %0" ::"m"(mxcsr));
    fxsave(fpu_init_state);

    kernel_cr3 = read_cr3();
    kernel_thread.id = 0;
    kernel_thread.state = State::READY;
    memcpy(kernel_thread.fpu, fpu_init_state, 512);
    cur = &kernel_thread;
    threads.push(&kernel_thread);
    slice_start = pit::ticks();
}

sched::Thread* sched::current() { return cur; }

sched::Thread* sched::create_user(Process* p, uint64_t entry, uint64_t user_stack, uint64_t arg) {
    auto* t = new Thread();
    t->id = next_id++;
    t->proc = p;
    t->cr3 = p->pml4_phys;
    t->kstack = (uint8_t*)mm::kmalloc(KSTACK);
    if (!t->kstack) { delete t; return nullptr; }
    t->kstack_top = ((uint64_t)t->kstack + KSTACK) & ~15ull;
    memcpy(t->fpu, fpu_init_state, 512);
    // contexto inicial: al "volver" de la interrupción entra en el programa (anillo 3)
    auto* f = (Frame*)(t->kstack_top - sizeof(Frame));
    memset(f, 0, sizeof(Frame));
    f->rip = entry;
    f->cs = gdt::UCODE | 3;
    f->rflags = 0x202;
    f->rsp = user_stack;
    f->ss = gdt::UDATA | 3;
    f->rdi = arg;
    t->frame = f;
    IrqGuard g;
    threads.push(t);
    return t;
}

void sched::destroy(Thread* t) {
    if (!t || t == cur || t == &kernel_thread) return;
    {
        IrqGuard g;
        threads.remove(t);
    }
    mm::kfree(t->kstack);
    delete t;
}

void sched::yield() { asm volatile("int $0x81" ::: "memory"); }

void sched::sleep(uint32_t ms) {
    cur->state = State::BLOCKED;
    cur->wake_at = pit::ticks() + ms;
    cur->wait_cond = nullptr;
    while (cur->state == State::BLOCKED && pit::ticks() < cur->wake_at) yield();
    cur->state = State::READY;
    cur->wake_at = 0;
}

void sched::wait(bool (*cond)(void*), void* ctx) {
    while (!cond(ctx)) {
        cur->state = State::BLOCKED;
        cur->wait_cond = cond;
        cur->wait_ctx = ctx;
        cur->wake_at = 0;
        yield();
        cur->state = State::READY;
        cur->wait_cond = nullptr;
        if (cur->proc && cur->proc->dying) return;
    }
}

bool sched::others_ready() {
    for (Thread* t : threads)
        if (t != &kernel_thread && runnable(t)) return true;
    return false;
}

void sched::idle() {
    if (others_ready()) yield();
    else hlt();
}

Frame* sched::schedule(Frame* f) {
    cur->frame = f;
    Thread* next = pick(false);
    switch_to(next);
    return cur->frame;
}

Frame* sched::preempt(Frame* f, bool to_kernel) {
    // sólo se llega aquí si la interrupción llegó con un programa en marcha (anillo 3)
    if (!to_kernel && pit::ticks() - slice_start < QUANTUM_MS) return f;
    cur->frame = f;
    Thread* next = pick(to_kernel);
    switch_to(next);
    return cur->frame;
}

#include "arch/idt.h"
#include "arch/gdt.h"
#include "arch/io.h"
#include "lib/log.h"
#include "lib/string.h"
#include "proc/sched.h"
#include "proc/process.h"
#include "proc/syscall.h"

namespace {

struct [[gnu::packed]] Gate {
    uint16_t off_lo;
    uint16_t sel;
    uint8_t ist;
    uint8_t attr;
    uint16_t off_mid;
    uint32_t off_hi;
    uint32_t zero;
};

struct [[gnu::packed]] Idtr {
    uint16_t limit;
    uint64_t base;
};

alignas(16) Gate gates[256];
IrqHandler irq_handlers[16];
struct Shared { idt::SharedIrqHandler fn; void* ctx; };
constexpr int MAX_SHARED = 4;
Shared shared_handlers[16][MAX_SHARED];

const char* exc_names[32] = {
    "División por cero", "Depuración", "NMI", "Punto de ruptura", "Desbordamiento",
    "Fuera de límites", "Instrucción inválida", "Dispositivo no disponible",
    "Doble fallo", "Segmento de coprocesador", "TSS inválido", "Segmento no presente",
    "Fallo de pila", "Protección general", "Fallo de página", "Reservado",
    "Error x87", "Comprobación de alineación", "Comprobación de máquina", "Error SIMD",
    "Virtualización", "Protección de control", "Reservado", "Reservado", "Reservado",
    "Reservado", "Reservado", "Reservado", "Inyección hipervisor", "VMM", "Seguridad", "Reservado"};

void set_gate(int v, uint64_t handler, uint8_t ist, uint8_t attr = 0x8E) {
    gates[v].off_lo = handler & 0xFFFF;
    gates[v].sel = gdt::KCODE;
    gates[v].ist = ist;
    gates[v].attr = attr;
    gates[v].off_mid = (handler >> 16) & 0xFFFF;
    gates[v].off_hi = handler >> 32;
    gates[v].zero = 0;
}

}

extern "C" uint64_t isr_stub_table[256];

extern "C" Frame* isr_dispatch(Frame* f) {
    bool from_user = (f->cs & 3) == 3;
    if (f->vector < 32 && from_user) return process::fault(f, exc_names[f->vector]);   // fallo de un programa
    if (f->vector == 0x80) {   // llamada al sistema de un programa .run
        syscall::dispatch(f);
        return f;
    }
    if (f->vector == 0x81) return sched::schedule(f);   // cesión voluntaria (punto seguro)
    if (f->vector < 32) {
        uint64_t cr2 = f->vector == 14 ? read_cr2() : 0;
        panic("Excepción %lu (%s)\nError=%lx RIP=%lx CR2=%lx\nRSP=%lx RAX=%lx RBX=%lx\nRCX=%lx RDX=%lx RSI=%lx RDI=%lx",
              f->vector, exc_names[f->vector], f->error, f->rip, cr2, f->rsp, f->rax, f->rbx,
              f->rcx, f->rdx, f->rsi, f->rdi);
    }
    if (f->vector >= 32 && f->vector < 48) {
        int irq = (int)f->vector - 32;
        // IRQ espurias del PIC
        if (irq == 7) { outb(0x20, 0x0B); if (!(inb(0x20) & 0x80)) return f; }
        if (irq == 15) { outb(0xA0, 0x0B); if (!(inb(0xA0) & 0x80)) { outb(0x20, 0x20); return f; } }
        if (irq_handlers[irq]) irq_handlers[irq](f);
        for (auto& s : shared_handlers[irq])
            if (s.fn) s.fn(s.ctx);
        pic::eoi(irq);
        // un programa interrumpido: el reloj reparte el tiempo y el teclado o el ratón dan
        // paso enseguida a la interfaz
        if (from_user) return sched::preempt(f, irq != 0);
    }
    return f;
}

void idt::init() {
    for (int i = 0; i < 256; i++) {
        uint8_t ist = 0;
        if (i == 8) ist = 1;
        if (i == 2 || i == 18) ist = 2;
        set_gate(i, isr_stub_table[i], ist);
    }
    set_gate(0x80, isr_stub_table[0x80], 0, 0xEE);   // los programas (anillo 3) pueden usarla
    Idtr idtr{sizeof(gates) - 1, (uint64_t)gates};
    asm volatile("lidt %0" ::"m"(idtr));
}

void idt::set_irq_handler(int irq, IrqHandler h) {
    irq_handlers[irq] = h;
    pic::unmask(irq);
}

bool idt::add_irq_handler(int irq, SharedIrqHandler h, void* ctx) {
    if (irq < 0 || irq >= 16) return false;
    IrqGuard g;
    for (auto& s : shared_handlers[irq]) {
        if (s.fn) continue;
        s = {h, ctx};
        pic::unmask(irq);
        return true;
    }
    return false;
}

void idt::remove_irq_handler(int irq, SharedIrqHandler h, void* ctx) {
    if (irq < 0 || irq >= 16) return;
    IrqGuard g;
    bool any = irq_handlers[irq] != nullptr;
    for (auto& s : shared_handlers[irq]) {
        if (s.fn == h && s.ctx == ctx) s = {nullptr, nullptr};
        if (s.fn) any = true;
    }
    if (!any && irq != 2) pic::mask(irq);
}

// --------------------------------------------------------------------------
// PIC 8259
// --------------------------------------------------------------------------
void pic::init() {
    outb(0x20, 0x11); io_wait();
    outb(0xA0, 0x11); io_wait();
    outb(0x21, 32); io_wait();
    outb(0xA1, 40); io_wait();
    outb(0x21, 4); io_wait();
    outb(0xA1, 2); io_wait();
    outb(0x21, 1); io_wait();
    outb(0xA1, 1); io_wait();
    outb(0x21, 0xFB); // todo enmascarado salvo la cascada (IRQ2)
    outb(0xA1, 0xFF);
}

void pic::mask(int irq) {
    uint16_t port = irq < 8 ? 0x21 : 0xA1;
    outb(port, inb(port) | (1 << (irq & 7)));
}

void pic::unmask(int irq) {
    uint16_t port = irq < 8 ? 0x21 : 0xA1;
    outb(port, inb(port) & ~(1 << (irq & 7)));
}

void pic::eoi(int irq) {
    if (irq >= 8) outb(0xA0, 0x20);
    outb(0x20, 0x20);
}

// --------------------------------------------------------------------------
// PIT 8254
// --------------------------------------------------------------------------
static volatile uint64_t tick_count = 0;
static uint32_t ms_per_tick = 1;

static void (*tick_hook)() = nullptr;

static void pit_irq(Frame*) {
    tick_count += ms_per_tick;
    if (tick_hook) tick_hook();
}

void pit::set_hook(void (*fn)()) { tick_hook = fn; }

void pit::init(uint32_t hz) {
    uint32_t div = 1193182 / hz;
    ms_per_tick = 1000 / hz;
    if (!ms_per_tick) ms_per_tick = 1;
    outb(0x43, 0x36);
    outb(0x40, div & 0xFF);
    outb(0x40, (div >> 8) & 0xFF);
    idt::set_irq_handler(0, pit_irq);
}

uint64_t pit::ticks() { return tick_count; }

void pit::sleep_ms(uint64_t ms) {
    uint64_t end = tick_count + ms;
    while (tick_count < end) hlt();
}

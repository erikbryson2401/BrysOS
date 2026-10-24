// Controlador empotrado: órdenes de lectura (0x80) y escritura (0x81) por los puertos de datos
// y de estado, esperando a que el EC vacíe su búfer de entrada (IBF) o llene el de salida (OBF)
#include "acpi/ec.h"
#include "arch/io.h"
#include "lib/log.h"

namespace {
uint16_t data = 0, cmd = 0;
bool ok = false;
uint32_t n_timeouts = 0;
int consecutive = 0;   // si deja de contestar, se da por perdido (no bloquea el sistema)

constexpr uint8_t OBF = 1, IBF = 2;

bool wait_ibf_clear() {
    for (int i = 0; i < 50000; i++) {   // ~50 ms
        if (!(inb(cmd) & IBF)) return true;
        io_wait();
    }
    return false;
}
bool wait_obf_set() {
    for (int i = 0; i < 50000; i++) {
        if (inb(cmd) & OBF) return true;
        io_wait();
    }
    return false;
}

bool failed() {
    n_timeouts++;
    if (++consecutive >= 8) {
        ok = false;
        klog("EC: no contesta; se deja de usar\n");
    }
    return false;
}

struct IrqOff {   // la transacción completa sin interrupciones
    uint64_t f;
    IrqOff() { asm volatile("pushfq; pop %0; cli" : "=r"(f)::"memory"); }
    ~IrqOff() { if (f & 0x200) asm volatile("sti" ::: "memory"); }
};
}

void ec::init(uint16_t d, uint16_t c) {
    data = d;
    cmd = c;
    uint8_t st = inb(cmd);
    ok = st != 0xFF;
    if (ok && (st & OBF)) (void)inb(data);   // dato antiguo
    klog("EC: puertos %x/%x, estado %x%s\n", d, c, st, ok ? "" : " (no hay)");
}

bool ec::present() { return ok; }
uint32_t ec::timeouts() { return n_timeouts; }

bool ec::read(uint8_t addr, uint8_t& v) {
    v = 0;
    if (!ok) return false;
    IrqOff g;
    if (inb(cmd) & OBF) (void)inb(data);
    if (!wait_ibf_clear()) return failed();
    outb(cmd, 0x80);
    if (!wait_ibf_clear()) return failed();
    outb(data, addr);
    if (!wait_obf_set()) return failed();
    v = inb(data);
    consecutive = 0;
    return true;
}

bool ec::write(uint8_t addr, uint8_t v) {
    if (!ok) return false;
    IrqOff g;
    if (!wait_ibf_clear()) return failed();
    outb(cmd, 0x81);
    if (!wait_ibf_clear()) return failed();
    outb(data, addr);
    if (!wait_ibf_clear()) return failed();
    outb(data, v);
    if (!wait_ibf_clear()) return failed();
    consecutive = 0;
    return true;
}

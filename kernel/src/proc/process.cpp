#include "proc/process.h"
#include "gui/wm.h"
#include "fs/vfs.h"
#include "mm/memory.h"
#include "arch/io.h"
#include "lib/log.h"
#include "lib/i18n.h"
#include "lib/notice.h"
#include "lib/string.h"
#include "sys/paths.h"
#include "kernel.h"
#include "proc/linux.h"

// Programas incluidos (tools/genembed.py, como las copias de reserva de las .bsl)
struct EmbeddedRun {
    const char* name;
    const uint8_t* data;
    uint64_t size;
};
extern "C" const EmbeddedRun run_embedded[];
extern "C" const uint64_t run_embedded_count;

namespace {
constexpr uint64_t ADDR = 0x000FFFFFFFFFF000ull;
constexpr uint64_t PTE_OWNED = 1ull << 9;          // bit libre: página propia del proceso
constexpr uint64_t USER_TOP = 0x0000800000000000ull;
constexpr uint64_t HEAP_LIMIT = 0x40000000ull;     // el montón no pasa de 1 GiB
constexpr uint64_t STACK_BYTES = 1024 * 1024;

Vec<Process*> procs;
int next_pid = 1;

uint64_t* kernel_pml4() {
    static uint64_t* k = nullptr;
    if (!k) k = (uint64_t*)mm::phys_to_virt(read_cr3() & ADDR);
    return k;
}

uint64_t* table_of(uint64_t e) { return (uint64_t*)mm::phys_to_virt(e & ADDR); }

// Entrada de la tabla de páginas de último nivel para 'virt' (creando las intermedias)
uint64_t* pte(Process* p, uint64_t virt, bool create) {
    uint64_t* t = p->pml4;
    for (int level = 3; level >= 1; level--) {
        uint64_t idx = (virt >> (12 + 9 * level)) & 511;
        if (!(t[idx] & mm::PTE_PRESENT)) {
            if (!create) return nullptr;
            uint64_t n = mm::alloc_page();
            if (!n) return nullptr;
            memset(mm::phys_to_virt(n), 0, 4096);
            t[idx] = n | mm::PTE_PRESENT | mm::PTE_WRITE | mm::PTE_USER;
        }
        t = table_of(t[idx]);
    }
    return &t[(virt >> 12) & 511];
}

void free_space(Process* p) {
    if (!p->pml4) return;
    for (int i = 0; i < 256; i++) {
        if (!(p->pml4[i] & mm::PTE_PRESENT)) continue;
        uint64_t* pdpt = table_of(p->pml4[i]);
        for (int j = 0; j < 512; j++) {
            if (!(pdpt[j] & mm::PTE_PRESENT)) continue;
            uint64_t* pd = table_of(pdpt[j]);
            for (int k = 0; k < 512; k++) {
                if (!(pd[k] & mm::PTE_PRESENT)) continue;
                uint64_t* pt = table_of(pd[k]);
                for (int l = 0; l < 512; l++)
                    if ((pt[l] & mm::PTE_PRESENT) && (pt[l] & PTE_OWNED)) mm::free_pages(pt[l] & ADDR, 1);
                mm::free_pages(pd[k] & ADDR, 1);
            }
            mm::free_pages(pdpt[j] & ADDR, 1);
        }
        mm::free_pages(p->pml4[i] & ADDR, 1);
    }
    for (size_t i = 0; i < p->canvases.size(); i++) mm::free_pages(p->canvases[i], p->canvas_pages[i]);
    mm::free_pages(p->pml4_phys, 1);
    p->pml4 = nullptr;
}

struct Ehdr {
    uint8_t ident[16];
    uint16_t type, machine;
    uint32_t version;
    uint64_t entry, phoff, shoff;
    uint32_t flags;
    uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
};
struct Phdr {
    uint32_t type, flags;
    uint64_t offset, vaddr, paddr, filesz, memsz, align;
};

bool load_elf(Process* p, const uint8_t* d, uint64_t n, uint64_t& entry, String& err) {
    auto* eh = (const Ehdr*)d;
    if (n < sizeof(Ehdr) || memcmp(eh->ident, "\x7f" "ELF", 4) || eh->ident[4] != 2 || eh->type != 2 || eh->machine != 62 ||
        eh->phoff + (uint64_t)eh->phnum * sizeof(Phdr) > n) {
        err = T("No es un programa .run válido.");
        return false;
    }
    uint64_t end = 0;
    auto* ph = (const Phdr*)(d + eh->phoff);
    for (int i = 0; i < eh->phnum; i++) {
        if (ph[i].type != 1) continue;   // PT_LOAD
        uint64_t va = ph[i].vaddr, sz = ph[i].memsz;
        if (va < RUN_BASE || va + sz > HEAP_LIMIT || va + sz < va || ph[i].offset + ph[i].filesz > n || ph[i].filesz > sz) {
            err = T("El programa .run está dañado o no es para BrysOS.");
            return false;
        }
        for (uint64_t pg = va & ~0xFFFull; pg < va + sz; pg += 4096) {
            uint64_t* e = pte(p, pg, true);
            if (!e) { err = T("No hay memoria suficiente para el programa."); return false; }
            if (!(*e & mm::PTE_PRESENT)) {
                uint64_t ph_page = mm::alloc_page();
                if (!ph_page) { err = T("No hay memoria suficiente para el programa."); return false; }
                memset(mm::phys_to_virt(ph_page), 0, 4096);
                *e = ph_page | mm::PTE_PRESENT | mm::PTE_WRITE | mm::PTE_USER | PTE_OWNED;
            }
            // parte de este segmento que cae en la página
            uint64_t from = pg > va ? pg : va, to = pg + 4096 < va + ph[i].filesz ? pg + 4096 : va + ph[i].filesz;
            if (to > from)
                memcpy((uint8_t*)mm::phys_to_virt(*e & ADDR) + (from - pg), d + ph[i].offset + (from - va), to - from);
        }
        if (va + sz > end) end = va + sz;
        // cabeceras de programa en memoria (AT_PHDR del vector auxiliar de Linux)
        if (eh->phoff >= ph[i].offset && eh->phoff < ph[i].offset + ph[i].filesz) p->phdr = va + (eh->phoff - ph[i].offset);
    }
    if (!end || eh->entry < RUN_BASE || eh->entry >= end) { err = T("El programa .run está dañado o no es para BrysOS."); return false; }
    entry = eh->entry;
    p->phnum = eh->phnum;
    p->entry = eh->entry;
    p->brk_base = p->brk = (end + 0xFFFF) & ~0xFFFFull;
    return true;
}

void destroy_windows(Process* p) {
    if (p->msg_window) { wm::destroy(p->msg_window); p->msg_window = nullptr; }
    for (auto*& w : p->windows)
        if (w) { wm::destroy(w); w = nullptr; }
    p->controls.clear();
    p->control_types.clear();
}

String exe_name(const char* path) {
    String b = vfs::basename(path);
    int dot = b.rfind('.');
    return dot > 0 ? b.substr(0, (size_t)dot) : b;
}
}

const Vec<Process*>& process::list() { return procs; }
Process* process::current() { return sched::current()->proc; }

void process::sync_kernel_half(Process* p) {
    if (p->pml4) memcpy(p->pml4 + 256, kernel_pml4() + 256, 256 * 8);
}

bool process::map_page(Process* p, uint64_t virt, uint64_t phys, bool write, bool owned) {
    uint64_t* e = pte(p, virt, true);
    if (!e) return false;
    *e = (phys & ADDR) | mm::PTE_PRESENT | mm::PTE_USER | (write ? mm::PTE_WRITE : 0) | (owned ? PTE_OWNED : 0);
    if (process::current() == p) invlpg(virt);
    return true;
}

bool process::alloc_range(Process* p, uint64_t virt, uint64_t bytes) {
    for (uint64_t pg = virt & ~0xFFFull; pg < virt + bytes; pg += 4096) {
        uint64_t* e = pte(p, pg, true);
        if (!e) return false;
        if (*e & mm::PTE_PRESENT) continue;
        uint64_t ph = mm::alloc_page();
        if (!ph) return false;
        memset(mm::phys_to_virt(ph), 0, 4096);
        *e = ph | mm::PTE_PRESENT | mm::PTE_WRITE | mm::PTE_USER | PTE_OWNED;
        if (process::current() == p) invlpg(pg);
    }
    return true;
}

void process::free_range(Process* p, uint64_t virt, uint64_t bytes) {
    for (uint64_t pg = virt & ~0xFFFull; pg < virt + bytes; pg += 4096) {
        uint64_t* e = pte(p, pg, false);
        if (!e || !(*e & mm::PTE_PRESENT)) continue;
        if (*e & PTE_OWNED) mm::free_pages(*e & ADDR, 1);
        *e = 0;
        if (process::current() == p) invlpg(pg);
    }
}

bool process::poke(Process* p, uint64_t virt, const void* data, uint64_t len) {
    const uint8_t* s = (const uint8_t*)data;
    while (len) {
        uint64_t* e = pte(p, virt, false);
        if (!e || !(*e & mm::PTE_PRESENT)) return false;
        uint64_t off = virt & 0xFFF, n = min<uint64_t>(len, 4096 - off);
        memcpy((uint8_t*)mm::phys_to_virt(*e & ADDR) + off, s, n);
        virt += n; s += n; len -= n;
    }
    return true;
}

bool process::user_ok(Process* p, uint64_t addr, uint64_t len, bool write) {
    if (!p || addr < 0x1000 || addr >= USER_TOP || len > USER_TOP - addr) return false;
    if (!len) return true;
    for (uint64_t pg = addr & ~0xFFFull; pg < addr + len; pg += 4096) {
        uint64_t* e = pte(p, pg, false);
        if (!e || !(*e & mm::PTE_PRESENT) || !(*e & mm::PTE_USER) || (write && !(*e & mm::PTE_WRITE))) return false;
    }
    return true;
}

bool process::user_string(Process* p, uint64_t addr, String& out, size_t max) {
    out.clear();
    for (size_t i = 0; i < max; i++) {
        if ((i == 0 || ((addr + i) & 0xFFF) == 0) && !user_ok(p, addr + i, 1, false)) return false;
        char c = *(const char*)(addr + i);
        if (!c) return true;
        out.append(c);
    }
    return true;   // recortada
}

void process::push_event(Process* p, const Evento& e) {
    int next = (p->qtail + 1) % Process::QUEUE;
    if (next == p->qhead) return;   // cola llena: se pierde (el programa no lee)
    p->queue[p->qtail] = e;
    p->qtail = next;
    if (!p->waiting_since) p->waiting_since = pit::ticks();
}

bool process::pop_event(Process* p, Evento& e) {
    if (p->qhead == p->qtail) return false;
    e = p->queue[p->qhead];
    p->qhead = (p->qhead + 1) % Process::QUEUE;
    p->waiting_since = p->qhead == p->qtail ? 0 : pit::ticks();
    return true;
}

bool process::responding(Process* p) { return !p->waiting_since || pit::ticks() - p->waiting_since < 5000; }

Process* process::start(const char* path, const char* args, String& err) {
    uint8_t* data;
    uint32_t size;
    if (!vfs::read_file(path, data, size)) { err = vfs::error(); return nullptr; }
    auto* p = new Process();
    p->pid = next_pid++;
    p->path = vfs::normalize(path);
    p->name = exe_name(path);
    p->args = args ? args : "";
    p->started = pit::ticks();
    p->pml4_phys = mm::alloc_page();
    if (!p->pml4_phys) { mm::kfree(data); delete p; err = T("No hay memoria suficiente para el programa."); return nullptr; }
    p->pml4 = (uint64_t*)mm::phys_to_virt(p->pml4_phys);
    memset(p->pml4, 0, 4096);
    sync_kernel_half(p);
    uint64_t entry = 0;
    bool ok = load_elf(p, data, size, entry, err);
    mm::kfree(data);
    // pila, con la línea de órdenes al principio
    uint64_t stack_lo = RUN_PILA_TOPE - STACK_BYTES;
    if (ok && !alloc_range(p, stack_lo, STACK_BYTES)) { ok = false; err = T("No hay memoria suficiente para el programa."); }
    uint64_t arg_ptr = 0;
    if (ok) {
        size_t n = min(p->args.size(), (size_t)4000);
        arg_ptr = RUN_PILA_TOPE - 4096;
        uint64_t* e = pte(p, arg_ptr, false);
        char* k = (char*)mm::phys_to_virt(*e & ADDR);
        memcpy(k, p->args.c_str(), n);
        k[n] = 0;
    }
    // Debajo, la pila inicial que espera un programa de Linux (argc, argv, envp y vector
    // auxiliar). Un .run no la mira (usa rdi); un programa de Linux no mira rdi.
    uint64_t sp = (RUN_PILA_TOPE - 4096 - 64) & ~15ull;
    if (ok) {
        p->lx = linux_abi::create_state(p);
        if (!linux_abi::build_stack(p, sp)) { ok = false; err = T("No hay memoria suficiente para el programa."); }
    }
    if (ok) {
        p->thread = sched::create_user(p, entry, sp, arg_ptr);
        if (!p->thread) { ok = false; err = T("No hay memoria suficiente para el programa."); }
    }
    if (!ok) {
        linux_abi::destroy_state(p);
        free_space(p);
        delete p;
        return nullptr;
    }
    procs.push(p);
    klog("Programa: %s (proceso %d) iniciado\n", p->path.c_str(), p->pid);
    return p;
}

void process::exit_current(int code) {
    Process* p = current();
    p->exit_code = code;
    p->dying = true;
    sched::current()->state = sched::State::ZOMBIE;
    for (;;) sched::yield();
}

Frame* process::fault(Frame* f, const char* what) {
    Process* p = current();
    if (p) {
        p->dying = true;
        p->crashed = true;
        uint64_t cr2 = f->vector == 14 ? read_cr2() : 0;
        p->crash_info = String::format("%s (RIP=%lx, dirección %lx)", what, f->rip, cr2);
        klog("Programa: %s (proceso %d) ha fallado: %s\n", p->name.c_str(), p->pid, p->crash_info.c_str());
        sched::current()->state = sched::State::ZOMBIE;
    }
    return sched::schedule(f);
}

void process::kill(Process* p) {
    if (!p || p->dying) return;
    p->dying = true;
    p->exit_code = -1;
    if (p->thread && p->thread != sched::current()) p->thread->state = sched::State::ZOMBIE;
    klog("Programa: %s (proceso %d) finalizado por el usuario\n", p->name.c_str(), p->pid);
}

void process::reap() {
    for (size_t i = procs.size(); i > 0; i--) {
        Process* p = procs[i - 1];
        if (!p->dying || (p->thread && p->thread == sched::current())) continue;
        if (p->thread && p->thread->state != sched::State::ZOMBIE) p->thread->state = sched::State::ZOMBIE;
        destroy_windows(p);
        sched::destroy(p->thread);
        p->thread = nullptr;
        linux_abi::destroy_state(p);   // cierra sus archivos (guarda lo que escribió)
        free_space(p);
        if (p->crashed)
            notice::post(p->name.c_str(),
                         String::format(T("Este programa ha realizado una operación no válida y se cerrará.\n\n"
                                          "Si el problema continúa, póngase en contacto con su autor.\n\nDetalles: %s"),
                                        p->crash_info.c_str()).c_str(), true);
        klog("Programa: %s (proceso %d) terminado (código %d)\n", p->name.c_str(), p->pid, p->exit_code);
        procs.erase(i - 1);
        delete p;
    }
}

const char* process::programs_dir() { return "C:\\Archivos de programa"; }

// Copia en C:\Archivos de programa los programas incluidos cuando cambia la versión
void process::install_bundled() {
    String dir = programs_dir();
    String marker = vfs::join(dir.c_str(), "version.txt");
    uint8_t* d;
    uint32_t n;
    bool same = false;
    if (vfs::read_file(marker.c_str(), d, n)) {
        same = n >= strlen(BRYSOS_VERSION) && !memcmp(d, BRYSOS_VERSION, strlen(BRYSOS_VERSION));
        mm::kfree(d);
    }
    if (same) return;
    if (!vfs::is_dir(dir.c_str()) && !vfs::mkdir(dir.c_str())) return;
    for (uint64_t i = 0; i < run_embedded_count; i++) {
        const EmbeddedRun& r = run_embedded[i];
        String path = vfs::join(dir.c_str(), r.name);
        if (vfs::write_file(path.c_str(), r.data, (uint32_t)r.size)) klog("Programas: instalado %s\n", path.c_str());
    }
    vfs::write_file(marker.c_str(), BRYSOS_VERSION "\r\n", (uint32_t)strlen(BRYSOS_VERSION) + 2);
}

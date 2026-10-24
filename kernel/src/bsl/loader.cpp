// Cargador de bibliotecas .bsl: ELF64 compartido (ET_DYN) para x86-64, enlazado en el
// momento de la carga contra la tabla de exportaciones del núcleo.
#include "bsl/loader.h"
#include "boot/bootinfo.h"
#include "fs/vfs.h"
#include "sys/paths.h"
#include "mm/memory.h"
#include "lib/log.h"
#include "lib/string.h"

// Tabla generada al enlazar (tools/genexports.py): símbolos globales del núcleo por nombre
struct KernelExport {
    const char* name;
    void* addr;
};
extern "C" const KernelExport bsl_exports[];
extern "C" const uint64_t bsl_export_count;

// Copias de reserva de las bibliotecas dentro del núcleo (tools/genembed.py)
struct EmbeddedBsl {
    const char* name;
    const uint8_t* data;
    uint64_t size;
};
extern "C" const EmbeddedBsl bsl_embedded[];
extern "C" const uint64_t bsl_embedded_count;

namespace {
Vec<bsl::Library*> libs;
Vec<String> errs;

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
struct Dyn {
    int64_t tag;
    uint64_t val;
};
struct Sym {
    uint32_t name;
    uint8_t info, other;
    uint16_t shndx;
    uint64_t value, size;
};
struct Rela {
    uint64_t offset, info;
    int64_t addend;
};

constexpr uint32_t PT_LOAD = 1, PT_DYNAMIC = 2;
constexpr int64_t DT_NULL = 0, DT_PLTRELSZ = 2, DT_HASH = 4, DT_STRTAB = 5, DT_SYMTAB = 6, DT_RELA = 7,
                  DT_RELASZ = 8, DT_RELAENT = 9, DT_JMPREL = 23, DT_INIT_ARRAY = 25, DT_INIT_ARRAYSZ = 27;
constexpr uint32_t R_NONE = 0, R_64 = 1, R_GLOB_DAT = 6, R_JUMP_SLOT = 7, R_RELATIVE = 8;

void* kernel_symbol(const char* name) {
    uint64_t lo = 0, hi = bsl_export_count;
    while (lo < hi) {
        uint64_t mid = (lo + hi) / 2;
        int c = strcmp(bsl_exports[mid].name, name);
        if (c == 0) return bsl_exports[mid].addr;
        if (c < 0) lo = mid + 1;
        else hi = mid;
    }
    return nullptr;
}

const char* basename_of(const char* path) {
    const char* b = path;
    for (const char* p = path; *p; p++)
        if (*p == '\\' || *p == '/') b = p + 1;
    return b;
}

bool loaded(const char* file) {
    for (auto* l : libs)
        if (!stricmp(l->file.c_str(), file)) return true;
    return false;
}
}

bool bsl::load(const char* file, const uint8_t* data, uint64_t size, const char* origin, String& err) {
    auto* eh = (const Ehdr*)data;
    if (size < sizeof(Ehdr) || memcmp(eh->ident, "\x7f" "ELF", 4) || eh->ident[4] != 2 || eh->type != 3 ||
        eh->machine != 62 || eh->phoff + (uint64_t)eh->phnum * sizeof(Phdr) > size) {
        err = "no es una biblioteca .bsl válida";
        return false;
    }
    auto* ph = (const Phdr*)(data + eh->phoff);
    uint64_t span = 0;
    const Phdr* dynph = nullptr;
    for (int i = 0; i < eh->phnum; i++) {
        if (ph[i].type == PT_LOAD) {
            if (ph[i].offset + ph[i].filesz > size) { err = "archivo incompleto"; return false; }
            span = max(span, ph[i].vaddr + ph[i].memsz);
        }
        if (ph[i].type == PT_DYNAMIC) dynph = &ph[i];
    }
    if (!span || !dynph || span > (64u << 20)) { err = "estructura no válida"; return false; }

    // memoria alineada a página (el montón no marca nada como no ejecutable)
    auto* raw = (uint8_t*)mm::kcalloc(span + 4096);
    if (!raw) { err = "memoria insuficiente"; return false; }
    uint8_t* base = (uint8_t*)(((uint64_t)raw + 4095) & ~4095ull);
    for (int i = 0; i < eh->phnum; i++)
        if (ph[i].type == PT_LOAD) memcpy(base + ph[i].vaddr, data + ph[i].offset, ph[i].filesz);

    // sección dinámica
    const Sym* symtab = nullptr;
    const char* strtab = nullptr;
    const uint32_t* hash = nullptr;
    uint64_t rela = 0, relasz = 0, relaent = sizeof(Rela), jmprel = 0, pltrelsz = 0, init = 0, initsz = 0;
    for (auto* d = (const Dyn*)(base + dynph->vaddr); d->tag != DT_NULL; d++) {
        switch (d->tag) {
        case DT_SYMTAB: symtab = (const Sym*)(base + d->val); break;
        case DT_STRTAB: strtab = (const char*)(base + d->val); break;
        case DT_HASH: hash = (const uint32_t*)(base + d->val); break;
        case DT_RELA: rela = d->val; break;
        case DT_RELASZ: relasz = d->val; break;
        case DT_RELAENT: relaent = d->val; break;
        case DT_JMPREL: jmprel = d->val; break;
        case DT_PLTRELSZ: pltrelsz = d->val; break;
        case DT_INIT_ARRAY: init = d->val; break;
        case DT_INIT_ARRAYSZ: initsz = d->val; break;
        }
    }
    auto fail = [&](const String& why) {
        err = why;
        mm::kfree(raw);
        return false;
    };
    if (!symtab || !strtab || !hash) return fail("faltan las tablas de símbolos");
    uint32_t nsyms = hash[1];

    // reubicaciones
    auto relocate = [&](uint64_t table, uint64_t bytes) -> bool {
        for (uint64_t off = 0; off + sizeof(Rela) <= bytes; off += relaent) {
            auto* r = (const Rela*)(base + table + off);
            uint32_t type = (uint32_t)r->info, si = (uint32_t)(r->info >> 32);
            auto* where = (uint64_t*)(base + r->offset);
            if (type == R_NONE) continue;
            if (type == R_RELATIVE) { *where = (uint64_t)base + r->addend; continue; }
            if (si >= nsyms) { err = "reubicación no válida"; return false; }
            const Sym& s = symtab[si];
            uint64_t value;
            if (s.shndx != 0) {
                value = (uint64_t)base + s.value;   // definido en la propia biblioteca
            } else {
                void* k = kernel_symbol(strtab + s.name);
                if (!k && (s.info >> 4) != 2) {     // los débiles pueden faltar
                    err = String("falta la función del núcleo ") + (strtab + s.name);
                    return false;
                }
                value = (uint64_t)k;
            }
            switch (type) {
            case R_64: *where = value + r->addend; break;
            case R_GLOB_DAT: case R_JUMP_SLOT: *where = value; break;
            default: err = String::format("tipo de reubicación %u no admitido", type); return false;
            }
        }
        return true;
    };
    if (!relocate(rela, relasz) || !relocate(jmprel, pltrelsz)) { mm::kfree(raw); return false; }

    // descriptor de la biblioteca
    const BslInfo* info = nullptr;
    for (uint32_t i = 0; i < nsyms; i++)
        if (symtab[i].shndx && !strcmp(strtab + symtab[i].name, "bsl_info")) info = (const BslInfo*)(base + symtab[i].value);
    if (!info || info->magic != BSL_MAGIC) return fail("no tiene el descriptor bsl_info");
    if (info->abi != BSL_ABI)
        return fail(String::format("es para otra versión del sistema (interfaz %u; este sistema usa la %u)", info->abi, BSL_ABI));
    for (auto* l : libs)
        if (!strcmp(l->name.c_str(), info->name)) return fail("ya hay otra biblioteca con el mismo nombre");

    // constructores globales y arranque
    for (uint64_t i = 0; i + 8 <= initsz; i += 8) {
        auto fn = *(void (**)())(base + init + i);
        if (fn) fn();
    }
    if (info->init && !info->init()) return fail("su inicialización ha fallado");

    auto* l = new Library();
    l->name = info->name;
    l->file = file;
    l->version = info->version;
    l->description = info->description;
    l->base = base;
    l->size = span;
    l->info = info;
    l->origin = origin;
    libs.push(l);
    klog("BSL: %s %s cargada (%s) en %p (%lu KB): %s\n", info->name, info->version, origin, base, span >> 10,
         info->description);
    return true;
}

void bsl::load_boot_modules() {
    for (int i = 0; i < boot_info.module_count; i++) {
        const BootModule& m = boot_info.modules[i];
        const char* name = basename_of(m.name);
        size_t n = strlen(name);
        if (n < 5 || stricmp(name + n - 4, ".bsl") || loaded(name)) continue;
        String err;
        if (!load(name, m.data(), m.size, boot_info.loader, err)) {
            klog("BSL: no se pudo cargar %s: %s\n", name, err.c_str());
            errs.push(String(name) + ": " + err);
        }
    }
}

void bsl::load_directory() {
    String dir = vfs::join(sys::DIR, "BSL");
    Vec<VfsEntry> ents;
    if (!vfs::list(dir.c_str(), ents)) return;
    for (auto& e : ents) {
        if (e.dir || vfs::extension(e.name.c_str()) != "BSL" || loaded(e.name.c_str())) continue;
        uint8_t* data;
        uint32_t size;
        String path = vfs::join(dir.c_str(), e.name.c_str());
        String err;
        if (!vfs::read_file(path.c_str(), data, size)) err = vfs::error();
        else {
            load(e.name.c_str(), data, size, "C:\\BrysOS\\BSL", err);
            mm::kfree(data);
        }
        if (!err.empty()) {
            klog("BSL: no se pudo cargar %s: %s\n", e.name.c_str(), err.c_str());
            errs.push(e.name + ": " + err);
        }
    }
}

void bsl::restore_missing() {
    String dir = sys::BSL_DIR;
    vfs::SystemWrite sw;
    for (uint64_t i = 0; i < bsl_embedded_count; i++) {
        const EmbeddedBsl& e = bsl_embedded[i];
        String path = vfs::join(dir.c_str(), e.name);
        if (vfs::exists(path.c_str())) continue;
        if (!vfs::is_dir(dir.c_str()) && !vfs::mkdir(dir.c_str())) {
            klog("BSL: no se puede crear %s: %s\n", dir.c_str(), vfs::error());
            return;
        }
        if (vfs::write_file(path.c_str(), e.data, (uint32_t)e.size))
            klog("BSL: %s no estaba en el disco; se ha restaurado desde la copia del núcleo\n", e.name);
        else
            klog("BSL: no se pudo restaurar %s: %s\n", e.name, vfs::error());
    }
}

void bsl::load_embedded() {
    for (uint64_t i = 0; i < bsl_embedded_count; i++) {
        const EmbeddedBsl& e = bsl_embedded[i];
        if (loaded(e.name)) continue;
        String err;
        if (!load(e.name, e.data, e.size, "copia del núcleo", err)) {
            klog("BSL: no se pudo cargar %s: %s\n", e.name, err.c_str());
            errs.push(String(e.name) + ": " + err);
        }
    }
}

const void* bsl::api(const char* name, uint32_t version) {
    for (auto* l : libs)
        if (!strcmp(l->name.c_str(), name)) return l->info->api ? l->info->api(version) : nullptr;
    return nullptr;
}

const Vec<bsl::Library*>& bsl::libraries() { return libs; }
const Vec<String>& bsl::errors() { return errs; }

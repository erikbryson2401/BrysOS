#include "boot/bootinfo.h"
#include "lib/string.h"

constinit BootInfo boot_info;

namespace {
constexpr uint32_t MB2_BOOT_MAGIC = 0x36D76289;

template <typename T> T rd(uint64_t phys) { return *(const T*)(phys + HHDM_BASE); }
}

const uint8_t* BootModule::data() const { return (const uint8_t*)(phys + HHDM_BASE); }

bool bootinfo_parse(uint32_t magic, uint32_t mbi) {
    if (magic != MB2_BOOT_MAGIC) return false;
    BootInfo& b = boot_info;
    uint32_t total = rd<uint32_t>(mbi);
    b.mbi_phys = mbi;
    b.mbi_size = total;
    uint64_t p = mbi + 8;
    uint64_t end = mbi + total;
    while (p + 8 <= end) {
        uint32_t type = rd<uint32_t>(p);
        uint32_t size = rd<uint32_t>(p + 4);
        if (type == 0) break;
        const uint8_t* t = (const uint8_t*)(p + HHDM_BASE);
        switch (type) {
        case 1: strlcpy(b.cmdline, (const char*)(t + 8), sizeof b.cmdline); break;
        case 2: strlcpy(b.loader, (const char*)(t + 8), sizeof b.loader); break;
        case 3:
            if (b.module_count < BootInfo::MAX_MODULES) {
                BootModule& m = b.modules[b.module_count++];
                uint32_t s = *(const uint32_t*)(t + 8), e = *(const uint32_t*)(t + 12);
                m.phys = s;
                m.size = e - s;
                strlcpy(m.name, (const char*)(t + 16), sizeof m.name);
            }
            break;
        case 6: {
            uint32_t esz = *(const uint32_t*)(t + 8);
            for (uint32_t off = 16; off + esz <= size && b.mem_count < BootInfo::MAX_MEM; off += esz) {
                MemRegion& r = b.mem[b.mem_count++];
                r.base = *(const uint64_t*)(t + off);
                r.length = *(const uint64_t*)(t + off + 8);
                r.type = *(const uint32_t*)(t + off + 16);
            }
            break;
        }
        case 8: {
            BootFramebuffer& f = b.fb;
            f.phys = *(const uint64_t*)(t + 8);
            f.pitch = *(const uint32_t*)(t + 16);
            f.width = *(const uint32_t*)(t + 20);
            f.height = *(const uint32_t*)(t + 24);
            f.bpp = t[28];
            uint8_t fbtype = t[29];
            if (fbtype == 1) {
                f.red_pos = t[32]; f.red_size = t[33];
                f.green_pos = t[34]; f.green_size = t[35];
                f.blue_pos = t[36]; f.blue_size = t[37];
            } else if (fbtype != 1) {
                // modo de texto o con paleta: no utilizable
                if (fbtype == 2) f.phys = 0;
            }
            break;
        }
        case 11: case 12: b.uefi = true; break;      // tabla de sistema EFI de 32/64 bits
        case 14: if (!b.rsdp_phys) b.rsdp_phys = p + 8; break;   // RSDP ACPI 1.0 (copia)
        case 15: b.rsdp_phys = p + 8; break;                      // RSDP ACPI 2.0+ (copia)
        }
        p += (size + 7) & ~7u;
    }
    return true;
}

bool cmdline_get(const char* key, char* out, size_t cap) {
    size_t kl = strlen(key);
    const char* s = boot_info.cmdline;
    while (*s) {
        while (*s == ' ') s++;
        if (!strncmp(s, key, kl) && s[kl] == '=') {
            s += kl + 1;
            size_t n = 0;
            while (*s && *s != ' ' && n + 1 < cap) out[n++] = *s++;
            out[n] = 0;
            return true;
        }
        while (*s && *s != ' ') s++;
    }
    return false;
}

int cmdline_int(const char* key, int def) {
    char v[16];
    if (!cmdline_get(key, v, sizeof v) || !v[0]) return def;
    int n = 0;
    for (const char* p = v; *p >= '0' && *p <= '9'; p++) n = n * 10 + (*p - '0');
    return n;
}

bool cmdline_has(const char* flag) {
    size_t fl = strlen(flag);
    const char* s = boot_info.cmdline;
    while (*s) {
        while (*s == ' ') s++;
        if (!strncmp(s, flag, fl) && (s[fl] == ' ' || s[fl] == 0)) return true;
        while (*s && *s != ' ') s++;
    }
    return false;
}

const BootModule* boot_module(const char* name) {
    for (int i = 0; i < boot_info.module_count; i++) {
        const char* n = boot_info.modules[i].name;
        // admite "nombre" o una ruta que termine en "/nombre"
        size_t ln = strlen(n), lk = strlen(name);
        if (!strcmp(n, name) || (ln > lk && n[ln - lk - 1] == '/' && !strcmp(n + ln - lk, name)))
            return &boot_info.modules[i];
    }
    return nullptr;
}

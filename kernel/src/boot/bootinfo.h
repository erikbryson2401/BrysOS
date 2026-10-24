#pragma once
#include <stdint.h>
#include <stddef.h>

// Información de arranque recibida del cargador (Multiboot2: GRUB o Limine)
struct MemRegion {
    uint64_t base = 0, length = 0;
    uint32_t type = 0;     // 1 = utilizable, 3 = ACPI recuperable, 4 = NVS, 5 = defectuosa, otros = reservada
};

struct BootModule {
    uint64_t phys = 0;
    uint64_t size = 0;
    char name[64] = {};
    const uint8_t* data() const;
};

struct BootFramebuffer {
    uint64_t phys = 0;
    uint32_t pitch = 0, width = 0, height = 0;
    uint8_t bpp = 0;
    uint8_t red_pos = 16, red_size = 8, green_pos = 8, green_size = 8, blue_pos = 0, blue_size = 8;
    void* virt = nullptr;
};

struct BootInfo {
    static constexpr int MAX_MEM = 128, MAX_MODULES = 32;
    MemRegion mem[MAX_MEM] = {};
    int mem_count = 0;
    BootModule modules[MAX_MODULES] = {};
    int module_count = 0;
    BootFramebuffer fb;
    uint64_t rsdp_phys = 0;
    bool uefi = false;
    char cmdline[512] = {};
    char loader[64] = {};
    uint64_t mbi_phys = 0, mbi_size = 0;
};

extern BootInfo boot_info;
constexpr uint64_t HHDM_BASE = 0xFFFF800000000000ull;

// Interpreta la estructura Multiboot2 (todavía con la memoria baja accesible por el HHDM)
bool bootinfo_parse(uint32_t magic, uint32_t mbi_phys);
// Valor de una opción "clave=valor" de la línea de órdenes (nullptr si no está)
bool cmdline_get(const char* key, char* out, size_t cap);
bool cmdline_has(const char* flag);
int cmdline_int(const char* key, int def);   // valor numérico de clave=N
const BootModule* boot_module(const char* name);

extern "C" char __kernel_phys_start[], __kernel_phys_end[];

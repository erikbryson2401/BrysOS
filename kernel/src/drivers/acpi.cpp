// ACPI: apagado (estado S5) y reinicio
#include "drivers/acpi.h"
#include "arch/io.h"
#include "arch/idt.h"
#include "mm/memory.h"
#include "lib/log.h"
#include "lib/string.h"
#include "kernel.h"

namespace {
struct [[gnu::packed]] Sdt {
    char sig[4];
    uint32_t length;
    uint8_t rev, checksum;
    char oem[6], oem_table[8];
    uint32_t oem_rev, creator, creator_rev;
};

struct [[gnu::packed]] GenericAddr {
    uint8_t space, bit_width, bit_offset, access;
    uint64_t address;
};

bool ready = false;
// Todas las tablas (para la orden ACPI, que las guarda en archivos)
Sdt* tables[64];
int ntables = 0;
// Se guarda una copia: la ventana de memoria donde se leen las tablas se reutiliza
void remember(Sdt* t) {
    if (!t || ntables >= 64 || t->length < 8 || t->length > (4u << 20)) return;
    for (int i = 0; i < ntables; i++)
        if (!memcmp(tables[i]->sig, t->sig, 4) && tables[i]->length == t->length && !memcmp(tables[i], t, t->length)) return;
    auto* copy = (Sdt*)mm::kmalloc(t->length);
    if (!copy) return;
    memcpy(copy, t, t->length);
    tables[ntables++] = copy;
}
uint16_t pm1a_cnt = 0, pm1b_cnt = 0, pm1a_evt = 0;
uint16_t slp_typa = 0, slp_typb = 0;
uint32_t smi_cmd = 0;
uint8_t acpi_enable = 0;
bool has_reset = false;
GenericAddr reset_reg{};
uint8_t reset_value = 0;

void* phys(uint64_t p, size_t len) {
    if (p >= mm::hhdm) p -= mm::hhdm;
    return mm::map_mmio(p, len);
}

Sdt* table(uint64_t p) {
    auto* h = (Sdt*)phys(p, sizeof(Sdt));
    return (Sdt*)phys(p, h->length);
}

// Busca el paquete \_S5 en el AML de la DSDT
void parse_s5(Sdt* dsdt) {
    uint8_t* aml = (uint8_t*)dsdt + sizeof(Sdt);
    uint32_t len = dsdt->length - sizeof(Sdt);
    for (uint32_t i = 0; i + 8 < len; i++) {
        if (memcmp(aml + i, "_S5_", 4)) continue;
        // debe ir precedido de NameOp (0x08) [o '\']
        if (!(i >= 1 && (aml[i - 1] == 0x08 || (i >= 2 && aml[i - 2] == 0x08 && aml[i - 1] == '\\')))) continue;
        uint8_t* p = aml + i + 4;
        if (*p != 0x12) continue; // PackageOp
        p++;
        int pkglen_bytes = ((*p & 0xC0) >> 6) + 1;
        p += pkglen_bytes;
        p++; // número de elementos
        auto read_val = [&](uint16_t& out) {
            if (*p == 0x0A) { p++; out = *p++; }        // BytePrefix
            else if (*p == 0x0B) { p++; out = (uint16_t)(p[0] | (p[1] << 8)); p += 2; }
            else if (*p == 0x00) { out = 0; p++; }       // ZeroOp
            else if (*p == 0x01) { out = 1; p++; }       // OneOp
            else { out = *p++; }
        };
        read_val(slp_typa);
        read_val(slp_typb);
        slp_typa &= 7;
        slp_typb &= 7;
        klog("ACPI: \\_S5 = %u/%u\n", slp_typa, slp_typb);
        ready = true;
        return;
    }
    klog("ACPI: no se encontró \\_S5 en la DSDT\n");
}
}

void acpi::init() {
    void* r = acpi_rsdp();
    if (!r) { klog("ACPI: no hay RSDP\n"); return; }
    auto* rsdp = (uint8_t*)phys((uint64_t)r, 36);
    if (memcmp(rsdp, "RSD PTR ", 8)) { klog("ACPI: RSDP no válida\n"); return; }
    uint8_t rev = rsdp[15];
    uint32_t rsdt = *(uint32_t*)(rsdp + 16);
    uint64_t xsdt = rev >= 2 ? *(uint64_t*)(rsdp + 24) : 0;
    Sdt* root = table(xsdt ? xsdt : rsdt);
    remember(root);
    int entry_size = xsdt ? 8 : 4;
    int n = (int)((root->length - sizeof(Sdt)) / entry_size);
    uint8_t* entries = (uint8_t*)root + sizeof(Sdt);
    Sdt* fadt = nullptr;
    for (int i = 0; i < n; i++) {
        uint64_t p = entry_size == 8 ? *(uint64_t*)(entries + i * 8) : *(uint32_t*)(entries + i * 4);
        Sdt* t = table(p);
        remember(t);
        if (!memcmp(t->sig, "FACP", 4)) fadt = t;
    }
    if (!fadt) { klog("ACPI: no hay FADT\n"); return; }
    uint8_t* f = (uint8_t*)fadt;
    uint32_t dsdt32 = *(uint32_t*)(f + 40);
    smi_cmd = *(uint32_t*)(f + 48);
    acpi_enable = f[52];
    pm1a_cnt = (uint16_t)*(uint32_t*)(f + 64);
    pm1b_cnt = (uint16_t)*(uint32_t*)(f + 68);
    pm1a_evt = (uint16_t)*(uint32_t*)(f + 56);
    uint64_t dsdt = dsdt32;
    if (fadt->length >= 148 && *(uint64_t*)(f + 140)) dsdt = *(uint64_t*)(f + 140);
    if (fadt->length >= 129) {
        uint32_t flags = *(uint32_t*)(f + 112);
        if (flags & (1 << 10)) {
            has_reset = true;
            memcpy(&reset_reg, f + 116, sizeof(GenericAddr));
            reset_value = f[128];
        }
    }
    klog("ACPI: revisión %u, PM1a_CNT=%x\n", rev, pm1a_cnt);
    if (dsdt) {
        remember(table(dsdt));
        parse_s5(table(dsdt));
    }
    // FACS (no lleva checksum ni la cabecera completa, pero sí firma y longitud)
    uint64_t facs = *(uint32_t*)(f + 36);
    if (fadt->length >= 140 && *(uint64_t*)(f + 132)) facs = *(uint64_t*)(f + 132);
    if (facs) remember(table(facs));
    power_button_pressed(); // descarta pulsaciones anteriores al arranque
}

bool acpi::shutdown() {
    if (!ready || !pm1a_cnt) return false;
    cli();
    // Activa el modo ACPI si el firmware no lo ha hecho
    if (smi_cmd && acpi_enable && !(inw(pm1a_cnt) & 1)) {
        outb((uint16_t)smi_cmd, acpi_enable);
        for (int i = 0; i < 3000000 && !(inw(pm1a_cnt) & 1); i++) io_wait();
    }
    outw(pm1a_cnt, (uint16_t)((inw(pm1a_cnt) & 0xE3FF) | (slp_typa << 10) | (1 << 13)));
    if (pm1b_cnt) outw(pm1b_cnt, (uint16_t)((inw(pm1b_cnt) & 0xE3FF) | (slp_typb << 10) | (1 << 13)));
    for (int i = 0; i < 10000000; i++) io_wait();
    // QEMU / Bochs antiguos
    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
    // VirtualBox
    outw(0x4004, 0x3400);
    sti();
    return false;
}

bool acpi::power_button_pressed() {
    if (!pm1a_evt) return false;
    uint16_t sts = inw(pm1a_evt);
    if (sts == 0xFFFF || !(sts & (1 << 8))) return false;
    outw(pm1a_evt, 1 << 8); // se borra escribiendo un 1
    return true;
}

void acpi::reboot() {
    cli();
    if (has_reset && reset_reg.space == 1) outb((uint16_t)reset_reg.address, reset_value);
    // Controlador de teclado
    for (int i = 0; i < 100000 && (inb(0x64) & 2); i++) {}
    outb(0x64, 0xFE);
    for (int i = 0; i < 1000000; i++) io_wait();
    // Reinicio por puerto 0xCF9
    outb(0xCF9, 0x06);
    for (int i = 0; i < 1000000; i++) io_wait();
    // Triple fallo
    struct [[gnu::packed]] { uint16_t l; uint64_t b; } idtr{0, 0};
    asm volatile("lidt %0; int $3" ::"m"(idtr));
    halt_forever();
}

int acpi::table_count() { return ntables; }

bool acpi::table_at(int i, char sig[5], const uint8_t*& data, uint32_t& length) {
    if (i < 0 || i >= ntables) return false;
    memcpy(sig, tables[i]->sig, 4);
    sig[4] = 0;
    data = (const uint8_t*)tables[i];
    length = tables[i]->length;
    return true;
}

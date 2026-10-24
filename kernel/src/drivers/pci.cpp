// Bus PCI mediante el mecanismo de configuración 1 (puertos 0xCF8/0xCFC)
#include "drivers/pci.h"
#include "arch/io.h"
#include "lib/log.h"

namespace {
Vec<PciDevice> devs;

uint32_t cfg_read(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off) {
    outl(0xCF8, 0x80000000u | (bus << 16) | (dev << 11) | (func << 8) | (off & 0xFC));
    return inl(0xCFC);
}
void cfg_write(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off, uint32_t v) {
    outl(0xCF8, 0x80000000u | (bus << 16) | (dev << 11) | (func << 8) | (off & 0xFC));
    outl(0xCFC, v);
}

void probe(uint8_t bus, uint8_t dev, uint8_t func) {
    uint32_t id = cfg_read(bus, dev, func, 0);
    if ((id & 0xFFFF) == 0xFFFF) return;
    PciDevice d;
    d.bus = bus; d.dev = dev; d.func = func;
    d.vendor = id & 0xFFFF;
    d.device = id >> 16;
    uint32_t c = cfg_read(bus, dev, func, 8);
    d.rev = c & 0xFF;
    d.prog_if = (c >> 8) & 0xFF;
    d.subclass = (c >> 16) & 0xFF;
    d.cls = c >> 24;
    d.irq = cfg_read(bus, dev, func, 0x3C) & 0xFF;
    devs.push(d);
}
}

void pci::init() {
    for (int bus = 0; bus < 256; bus++) {
        for (int dev = 0; dev < 32; dev++) {
            uint32_t id = cfg_read(bus, dev, 0, 0);
            if ((id & 0xFFFF) == 0xFFFF) continue;
            uint8_t hdr = (cfg_read(bus, dev, 0, 0x0C) >> 16) & 0xFF;
            int nfunc = (hdr & 0x80) ? 8 : 1;
            for (int f = 0; f < nfunc; f++) probe(bus, dev, f);
        }
    }
    klog("PCI: %lu dispositivos\n", (uint64_t)devs.size());
    for (auto& d : devs)
        klog("  %02x:%02x.%u %04x:%04x clase %02x.%02x.%02x %s\n", d.bus, d.dev, d.func, d.vendor, d.device,
             d.cls, d.subclass, d.prog_if, class_name(d.cls, d.subclass));
}

const Vec<PciDevice>& pci::devices() { return devs; }

uint32_t pci::read32(const PciDevice& d, uint8_t off) { return cfg_read(d.bus, d.dev, d.func, off); }
void pci::write32(const PciDevice& d, uint8_t off, uint32_t v) { cfg_write(d.bus, d.dev, d.func, off, v); }

uint16_t pci::read16(const PciDevice& d, uint8_t off) {
    return (cfg_read(d.bus, d.dev, d.func, off & 0xFC) >> ((off & 2) * 8)) & 0xFFFF;
}

void pci::write16(const PciDevice& d, uint8_t off, uint16_t v) {
    uint32_t old = cfg_read(d.bus, d.dev, d.func, off & 0xFC);
    int sh = (off & 2) * 8;
    old = (old & ~(0xFFFFu << sh)) | ((uint32_t)v << sh);
    cfg_write(d.bus, d.dev, d.func, off & 0xFC, old);
}

uint64_t pci::bar_address(const PciDevice& d, int bar) {
    uint32_t lo = read32(d, 0x10 + bar * 4);
    if (lo & 1) return 0;
    uint64_t addr = lo & ~0xFull;
    if (((lo >> 1) & 3) == 2) addr |= (uint64_t)read32(d, 0x14 + bar * 4) << 32;
    return addr;
}

uint32_t pci::bar_io(const PciDevice& d, int bar) {
    uint32_t v = read32(d, 0x10 + bar * 4);
    return (v & 1) ? (v & ~3u) : 0;
}

void pci::enable_bus_master(const PciDevice& d) {
    uint16_t cmd = read16(d, 0x04);
    cmd |= 0x06;          // espacio de memoria + bus master
    cmd &= ~0x400;        // interrupciones INTx permitidas (no se usan)
    write16(d, 0x04, cmd);
}

const char* pci::class_name(uint8_t cls, uint8_t sub) {
    switch (cls) {
    case 0x01:
        switch (sub) {
        case 0x01: return "Controladora IDE";
        case 0x06: return "Controladora SATA";
        case 0x08: return "Controladora NVMe";
        }
        return "Almacenamiento";
    case 0x02: return "Red";
    case 0x03: return "Pantalla";
    case 0x04: return "Multimedia";
    case 0x06:
        switch (sub) {
        case 0x00: return "Puente de host";
        case 0x01: return "Puente ISA";
        case 0x04: return "Puente PCI";
        }
        return "Puente";
    case 0x07: return "Comunicaciones";
    case 0x08: return "Periférico del sistema";
    case 0x0C:
        if (sub == 0x03) return "Controladora USB";
        if (sub == 0x05) return "SMBus";
        return "Bus serie";
    case 0x0D: return "Inalámbrico";
    }
    return "Otro";
}

void pci::write8(const PciDevice& d, uint8_t off, uint8_t v) {
    outl(0xCF8, 0x80000000u | (d.bus << 16) | (d.dev << 11) | (d.func << 8) | (off & 0xFC));
    outb((uint16_t)(0xCFC + (off & 3)), v);
}

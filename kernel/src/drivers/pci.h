#pragma once
#include <stdint.h>
#include "lib/vec.h"

struct PciDevice {
    uint8_t bus, dev, func;
    uint16_t vendor, device;
    uint8_t cls, subclass, prog_if, rev;
    uint8_t irq;
};

namespace pci {
void init();
const Vec<PciDevice>& devices();
uint32_t read32(const PciDevice& d, uint8_t off);
void write32(const PciDevice& d, uint8_t off, uint32_t v);
uint16_t read16(const PciDevice& d, uint8_t off);
void write16(const PciDevice& d, uint8_t off, uint16_t v);
void write8(const PciDevice& d, uint8_t off, uint8_t v);
uint64_t bar_address(const PciDevice& d, int bar);   // dirección física de un BAR de memoria
uint32_t bar_io(const PciDevice& d, int bar);          // puerto de un BAR de E/S
void enable_bus_master(const PciDevice& d);
const char* class_name(uint8_t cls, uint8_t sub);
}

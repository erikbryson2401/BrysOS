// Controladora IDE/ATA por E/S programada (respaldo cuando el SATA está en modo IDE
// o la máquina virtual usa un controlador PIIX)
#include "drivers/block.h"
#include "drivers/pci.h"
#include "arch/idt.h"
#include "arch/io.h"
#include "lib/log.h"
#include "lib/string.h"

namespace {
constexpr uint8_t ST_BSY = 0x80, ST_DRDY = 0x40, ST_DF = 0x20, ST_DRQ = 0x08, ST_ERR = 0x01;

class AtaDisk : public BlockDevice {
public:
    uint16_t io, ctrl;
    bool slave;
    bool lba48 = false;

    uint8_t status() { return inb(io + 7); }
    void delay400() { for (int i = 0; i < 4; i++) inb(ctrl); }

    bool wait_not_busy(uint64_t ms = 3000) {
        uint64_t end = pit::ticks() + ms;
        while (status() & ST_BSY)
            if (pit::ticks() > end) return false;
        return true;
    }

    bool wait_drq() {
        uint64_t end = pit::ticks() + 3000;
        for (;;) {
            uint8_t s = status();
            if (s & (ST_ERR | ST_DF)) return false;
            if (!(s & ST_BSY) && (s & ST_DRQ)) return true;
            if (pit::ticks() > end) return false;
        }
    }

    void select(uint64_t lba, bool use48) {
        outb(io + 6, (slave ? 0xF0 : 0xE0) | (use48 ? 0 : ((lba >> 24) & 0x0F)));
        delay400();
    }

    bool identify() {
        outb(io + 6, slave ? 0xB0 : 0xA0);
        delay400();
        outb(io + 2, 0); outb(io + 3, 0); outb(io + 4, 0); outb(io + 5, 0);
        outb(io + 7, 0xEC);
        if (status() == 0) return false;
        if (!wait_not_busy(1000)) return false;
        if (inb(io + 4) || inb(io + 5)) return false; // ATAPI o SATA: no es un disco ATA
        if (!wait_drq()) return false;
        uint16_t id[256];
        for (int i = 0; i < 256; i++) id[i] = inw(io);
        lba48 = id[83] & (1 << 10);
        sectors = lba48 ? *(uint64_t*)&id[100] : *(uint32_t*)&id[60];
        for (int i = 0; i < 20; i++) {
            model[i * 2] = (char)(id[27 + i] >> 8);
            model[i * 2 + 1] = (char)(id[27 + i] & 0xFF);
        }
        model[40] = 0;
        for (int i = 39; i >= 0 && model[i] == ' '; i--) model[i] = 0;
        return sectors > 0;
    }

    bool setup_cmd(uint64_t lba, uint32_t n, uint8_t cmd28, uint8_t cmd48) {
        if (!wait_not_busy()) return false;
        bool use48 = lba48 && (lba + n > 0x0FFFFFFF || n > 256);
        select(lba, use48);
        if (use48) {
            outb(io + 2, (n >> 8) & 0xFF);
            outb(io + 3, (lba >> 24) & 0xFF);
            outb(io + 4, (lba >> 32) & 0xFF);
            outb(io + 5, (lba >> 40) & 0xFF);
        }
        outb(io + 2, n & 0xFF);
        outb(io + 3, lba & 0xFF);
        outb(io + 4, (lba >> 8) & 0xFF);
        outb(io + 5, (lba >> 16) & 0xFF);
        outb(io + 7, use48 ? cmd48 : cmd28);
        return true;
    }

    bool read(uint64_t lba, uint32_t count, void* out) override {
        auto* p = (uint16_t*)out;
        while (count) {
            uint32_t n = min(count, 128u);
            if (!setup_cmd(lba, n, 0x20, 0x24)) return false;
            for (uint32_t s = 0; s < n; s++) {
                if (!wait_drq()) { klog("%s: error de lectura en LBA %lu\n", name, lba + s); return false; }
                for (int i = 0; i < 256; i++) *p++ = inw(io);
            }
            lba += n;
            count -= n;
        }
        return true;
    }

    bool write(uint64_t lba, uint32_t count, const void* in) override {
        auto* p = (const uint16_t*)in;
        while (count) {
            uint32_t n = min(count, 128u);
            if (!setup_cmd(lba, n, 0x30, 0x34)) return false;
            for (uint32_t s = 0; s < n; s++) {
                if (!wait_drq()) { klog("%s: error de escritura en LBA %lu\n", name, lba + s); return false; }
                for (int i = 0; i < 256; i++) outw(io, *p++);
            }
            if (!wait_not_busy()) return false;
            lba += n;
            count -= n;
        }
        return true;
    }

    bool flush() override {
        select(0, false);
        outb(io + 7, lba48 ? 0xEA : 0xE7);
        return wait_not_busy(10000);
    }
};

int count = 0;

void probe_channel(uint16_t io, uint16_t ctrl) {
    if (inb(io + 7) == 0xFF) return; // canal flotante
    outb(ctrl, 0x02);                // sin interrupciones
    for (int s = 0; s < 2; s++) {
        auto* d = new AtaDisk();
        d->io = io;
        d->ctrl = ctrl;
        d->slave = s;
        if (d->identify()) {
            ksnprintf(d->name, sizeof d->name, "IDE%d", count++);
            klog("ATA: %s \"%s\", %lu MiB\n", s ? "esclavo" : "maestro", d->model, d->sectors / 2048);
            block::add_disk(d);
        } else {
            delete d;
        }
    }
}
}

void ata::init() {
    for (auto& d : pci::devices()) {
        if (d.cls != 0x01 || d.subclass != 0x01) continue;
        // bit 0/2 del prog_if: canal en modo nativo PCI
        uint16_t io0 = 0x1F0, ct0 = 0x3F6, io1 = 0x170, ct1 = 0x376;
        if (d.prog_if & 1) { io0 = (uint16_t)pci::bar_io(d, 0); ct0 = (uint16_t)pci::bar_io(d, 1) + 2; }
        if (d.prog_if & 4) { io1 = (uint16_t)pci::bar_io(d, 2); ct1 = (uint16_t)pci::bar_io(d, 3) + 2; }
        pci::enable_bus_master(d);
        if (io0) probe_channel(io0, ct0);
        if (io1) probe_channel(io1, ct1);
    }
}

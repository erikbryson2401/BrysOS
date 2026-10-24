// AC'97 (Intel ICH y compatibles): salida PCM por bus master
#include "drivers/audio.h"
#include "drivers/pci.h"
#include "mm/memory.h"
#include "arch/io.h"
#include "arch/idt.h"
#include "lib/log.h"
#include "lib/string.h"

namespace {
uint16_t nam = 0, nabm = 0;
uint32_t entry_bytes = 0;
constexpr uint16_t PO_BDBAR = 0x10, PO_CIV = 0x14, PO_LVI = 0x15, PO_SR = 0x16, PO_PICB = 0x18, PO_CR = 0x1B,
                   GLOB_CNT = 0x2C;
}

bool audio::ac97::probe() {
    const PciDevice* dev = nullptr;
    for (auto& d : pci::devices())
        if (d.cls == 0x04 && d.subclass == 0x01) { dev = &d; break; }
    if (!dev) return false;
    nam = (uint16_t)pci::bar_io(*dev, 0);
    nabm = (uint16_t)pci::bar_io(*dev, 1);
    if (!nam || !nabm) return false;
    uint16_t cmd = pci::read16(*dev, 0x04);
    pci::write16(*dev, 0x04, (uint16_t)(cmd | 0x05));
    // arranque en frío del códec
    outl(nabm + GLOB_CNT, 0x02);
    pit::sleep_ms(20);
    outw(nam + 0x00, 0);              // reinicio de registros del códec
    pit::sleep_ms(10);
    outw(nam + 0x02, 0x0000);         // volumen maestro al máximo, sin silencio
    outw(nam + 0x04, 0x0000);         // auriculares
    outw(nam + 0x18, 0x0808);         // PCM de salida a 0 dB
    if (inw(nam + 0x28) & 1) {        // velocidad variable: se fija a 48 kHz
        outw(nam + 0x2A, (uint16_t)(inw(nam + 0x2A) | 1));
        outw(nam + 0x2C, 48000);
    }
    klog("Sonido: AC'97 en %02x:%02x.%u\n", dev->bus, dev->dev, dev->func);
    return true;
}

void audio::ac97::stop() {
    if (!nabm) return;
    outb(nabm + PO_CR, 0);
    outb(nabm + PO_CR, 0x02);   // reinicio del canal
    for (int i = 0; i < 1000 && (inb(nabm + PO_CR) & 2); i++) io_wait();
}

// Búfer circular: 32 descriptores; el último válido (LVI) se mantiene siempre por delante
// del actual para que la controladora no se detenga nunca
bool audio::ac97::start_ring(uint64_t bdl_phys, uint64_t ring_phys, uint32_t bytes) {
    if (!nabm) return false;
    stop();
    auto* bdl = (uint32_t*)mm::phys_to_virt(bdl_phys);
    entry_bytes = bytes / 32;
    for (int n = 0; n < 32; n++) {
        bdl[n * 2] = (uint32_t)(ring_phys + (uint64_t)n * entry_bytes);
        bdl[n * 2 + 1] = entry_bytes / 2;   // en muestras de 16 bits
    }
    outl(nabm + PO_BDBAR, (uint32_t)bdl_phys);
    outb(nabm + PO_LVI, 31);
    outw(nabm + PO_SR, 0x1C);    // borra estados
    outb(nabm + PO_CR, 0x01);    // en marcha
    return true;
}

uint32_t audio::ac97::ring_pos() {
    if (!nabm || !entry_bytes) return 0;
    uint8_t civ = inb(nabm + PO_CIV) & 31;
    uint16_t picb = inw(nabm + PO_PICB);   // muestras que faltan del descriptor actual
    uint32_t done = entry_bytes - (uint32_t)picb * 2;
    if (done > entry_bytes) done = 0;
    return civ * entry_bytes + done;
}

void audio::ac97::tick() {
    if (!nabm) return;
    uint8_t civ = inb(nabm + PO_CIV) & 31;
    outb(nabm + PO_LVI, (uint8_t)((civ + 31) & 31));
}

const char* audio::ac97::name() { return "AC'97"; }

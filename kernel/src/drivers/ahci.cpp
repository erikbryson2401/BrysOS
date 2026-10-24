// Controladora SATA AHCI 1.x: lectura/escritura DMA por sondeo (sin interrupciones)
#include "drivers/block.h"
#include "drivers/pci.h"
#include "arch/idt.h"
#include "arch/io.h"
#include "mm/memory.h"
#include "lib/log.h"
#include "lib/string.h"

namespace {
// Registros globales
constexpr uint32_t CAP = 0x00, GHC = 0x04, PI = 0x0C, VS = 0x10, CAP2 = 0x24, BOHC = 0x28;
// Registros de puerto (desplazamiento dentro del bloque de 0x80 bytes)
constexpr uint32_t P_CLB = 0x00, P_CLBU = 0x04, P_FB = 0x08, P_FBU = 0x0C, P_IS = 0x10, P_IE = 0x14,
                   P_CMD = 0x18, P_TFD = 0x20, P_SIG = 0x24, P_SSTS = 0x28, P_SERR = 0x30, P_CI = 0x38;
constexpr uint32_t CMD_ST = 1, CMD_SUD = 2, CMD_POD = 4, CMD_FRE = 0x10, CMD_FR = 0x4000, CMD_CR = 0x8000;
constexpr uint32_t TFD_BSY = 0x80, TFD_DRQ = 0x08, TFD_ERR = 0x01;
constexpr uint32_t IS_TFES = 1u << 30;
constexpr uint32_t MAX_SECTORS = 128;       // 64 KiB por orden
constexpr uint32_t BOUNCE_PAGES = 16;

struct [[gnu::packed]] CmdHeader {
    uint16_t flags;     // CFL[4:0], A, W, P, R, B, C
    uint16_t prdtl;
    uint32_t prdbc;
    uint32_t ctba;
    uint32_t ctbau;
    uint32_t rsv[4];
};

struct [[gnu::packed]] Prd {
    uint32_t dba, dbau, rsv, dbc;
};

struct [[gnu::packed]] CmdTable {
    uint8_t cfis[64];
    uint8_t acmd[16];
    uint8_t rsv[48];
    Prd prdt[8];
};

bool wait_until(uint64_t timeout_ms, auto cond) {
    uint64_t end = pit::ticks() + timeout_ms;
    while (pit::ticks() < end) {
        if (cond()) return true;
        asm volatile("pause");
    }
    return cond();
}

class AhciDisk : public BlockDevice {
public:
    volatile uint8_t* hba;
    int port;
    uint64_t clb_phys = 0, fb_phys = 0, ct_phys = 0, buf_phys = 0;
    uint8_t* buf = nullptr;
    bool lba48 = true;

    uint32_t rd(uint32_t off) { return *(volatile uint32_t*)(hba + 0x100 + port * 0x80 + off); }
    void wr(uint32_t off, uint32_t v) { *(volatile uint32_t*)(hba + 0x100 + port * 0x80 + off) = v; }

    bool stop() {
        wr(P_CMD, rd(P_CMD) & ~CMD_ST);
        if (!wait_until(600, [&] { return !(rd(P_CMD) & CMD_CR); })) return false;
        wr(P_CMD, rd(P_CMD) & ~CMD_FRE);
        return wait_until(600, [&] { return !(rd(P_CMD) & CMD_FR); });
    }

    bool setup() {
        if (!stop()) { klog("AHCI: el puerto %d no se detiene\n", port); return false; }
        clb_phys = mm::alloc_page();     // lista de órdenes (1 KiB) + FIS recibidos (256 B)
        ct_phys = mm::alloc_page();      // tabla de órdenes de la ranura 0
        buf_phys = mm::alloc_pages(BOUNCE_PAGES);
        if (!clb_phys || !ct_phys || !buf_phys) return false;
        fb_phys = clb_phys + 2048;
        buf = (uint8_t*)mm::phys_to_virt(buf_phys);
        wr(P_CLB, (uint32_t)clb_phys);
        wr(P_CLBU, (uint32_t)(clb_phys >> 32));
        wr(P_FB, (uint32_t)fb_phys);
        wr(P_FBU, (uint32_t)(fb_phys >> 32));
        auto* hdr = (CmdHeader*)mm::phys_to_virt(clb_phys);
        hdr[0].ctba = (uint32_t)ct_phys;
        hdr[0].ctbau = (uint32_t)(ct_phys >> 32);
        wr(P_SERR, 0xFFFFFFFF);
        wr(P_IS, 0xFFFFFFFF);
        wr(P_IE, 0);
        uint32_t cmd = rd(P_CMD) | CMD_POD | CMD_SUD;
        wr(P_CMD, cmd);
        wr(P_CMD, rd(P_CMD) | CMD_FRE);
        wait_until(200, [&] { return !(rd(P_TFD) & (TFD_BSY | TFD_DRQ)); });
        wr(P_CMD, rd(P_CMD) | CMD_ST);
        return true;
    }

    // Ejecuta una orden ATA con transferencia DMA a/desde el búfer intermedio
    bool exec(uint8_t command, uint64_t lba, uint32_t count, uint32_t bytes, bool write) {
        if (!wait_until(1000, [&] { return !(rd(P_TFD) & (TFD_BSY | TFD_DRQ)); })) {
            klog("AHCI%d: dispositivo ocupado (TFD=%x)\n", port, rd(P_TFD));
            return false;
        }
        wr(P_IS, 0xFFFFFFFF);
        auto* hdr = (CmdHeader*)mm::phys_to_virt(clb_phys);
        // CFL = 5 dwords y W si se escribe. Sin el bit C ("Clear Busy upon R_OK"): con él, una
        // controladora real da la orden por terminada antes de recibir los datos.
        hdr[0].flags = 5 | (write ? (1 << 6) : 0);
        hdr[0].prdtl = bytes ? 1 : 0;
        hdr[0].prdbc = 0;
        auto* ct = (CmdTable*)mm::phys_to_virt(ct_phys);
        memset(ct, 0, sizeof(CmdTable));
        if (bytes) {
            ct->prdt[0].dba = (uint32_t)buf_phys;
            ct->prdt[0].dbau = (uint32_t)(buf_phys >> 32);
            ct->prdt[0].dbc = bytes - 1;
        }
        uint8_t* f = ct->cfis;
        f[0] = 0x27;   // FIS Host a Dispositivo
        f[1] = 0x80;   // es una orden
        f[2] = command;
        f[4] = lba & 0xFF;
        f[5] = (lba >> 8) & 0xFF;
        f[6] = (lba >> 16) & 0xFF;
        f[7] = 0x40;   // modo LBA
        f[8] = (lba >> 24) & 0xFF;
        f[9] = (lba >> 32) & 0xFF;
        f[10] = (lba >> 40) & 0xFF;
        f[12] = count & 0xFF;
        f[13] = (count >> 8) & 0xFF;
        asm volatile("mfence" ::: "memory");
        wr(P_CI, 1);
        bool ok = wait_until(10000, [&] { return !(rd(P_CI) & 1) || (rd(P_IS) & IS_TFES); });
        if (!ok || (rd(P_IS) & IS_TFES) || (rd(P_TFD) & TFD_ERR)) {
            klog("AHCI%d: error en la orden %x LBA %lu (IS=%x TFD=%x SERR=%x SSTS=%x CMD=%x%s)\n", port, command, lba,
                 rd(P_IS), rd(P_TFD), rd(P_SERR), rd(P_SSTS), rd(P_CMD), ok ? "" : ", tiempo agotado");
            // recuperación: reinicia el motor del puerto
            stop();
            wr(P_SERR, 0xFFFFFFFF);
            wr(P_IS, 0xFFFFFFFF);
            wr(P_CMD, rd(P_CMD) | CMD_FRE);
            wr(P_CMD, rd(P_CMD) | CMD_ST);
            return false;
        }
        return true;
    }

    uint32_t sig() { return rd(P_SIG); }

    // Reinicio de la conexión (COMRESET): obliga al dispositivo a presentarse de nuevo
    void comreset() {
        stop();
        volatile uint32_t* sctl = (volatile uint32_t*)(hba + 0x100 + port * 0x80 + 0x2C);
        *sctl = (*sctl & ~0xFu) | 1;
        pit::sleep_ms(2);
        *sctl = *sctl & ~0xFu;
        wait_until(1000, [&] { return (rd(P_SSTS) & 0xF) == 3; });
        wr(P_SERR, 0xFFFFFFFF);
        wr(P_IS, 0xFFFFFFFF);
        wr(P_CMD, rd(P_CMD) | CMD_FRE);
        wait_until(1000, [&] { return !(rd(P_TFD) & (TFD_BSY | TFD_DRQ)); });
        wr(P_CMD, rd(P_CMD) | CMD_ST);
    }

    // La firma sólo es válida cuando el dispositivo ha enviado su primer FIS
    bool wait_signature(uint32_t ms) {
        return wait_until(ms, [&] { return sig() != 0xFFFFFFFF && !(rd(P_TFD) & TFD_BSY); });
    }

    bool identify() {
        memset(buf, 0, 512);
        if (!exec(0xEC, 0, 0, 512, false)) return false;
        auto* hdr = (CmdHeader*)mm::phys_to_virt(clb_phys);
        auto* id = (uint16_t*)buf;
        // datos de verdad: 512 bytes transferidos, palabra 0 de un dispositivo ATA y tamaño > 0
        if (hdr[0].prdbc != 512 && !id[0] && !id[83] && !id[60]) {
            klog("AHCI%d: IDENTIFY sin datos (bytes recibidos %u, búfer %lx)\n", port, hdr[0].prdbc, buf_phys);
            return false;
        }
        if (id[0] & 0x8000) { klog("AHCI%d: no es un disco ATA (palabra 0 = %x)\n", port, id[0]); return false; }
        lba48 = id[83] & (1 << 10);
        if (lba48) sectors = *(uint64_t*)&id[100];
        else sectors = *(uint32_t*)&id[60];
        // Modelo: palabras 27-46, bytes intercambiados
        for (int i = 0; i < 20; i++) {
            model[i * 2] = (char)(id[27 + i] >> 8);
            model[i * 2 + 1] = (char)(id[27 + i] & 0xFF);
        }
        model[40] = 0;
        for (int i = 39; i >= 0 && model[i] == ' '; i--) model[i] = 0;
        if (!sectors) { klog("AHCI%d: IDENTIFY indica tamaño 0\n", port); return false; }
        return true;
    }
    bool read(uint64_t lba, uint32_t count, void* out) override {
        auto* dst = (uint8_t*)out;
        while (count) {
            uint32_t n = min(count, MAX_SECTORS);
            uint8_t c = lba48 ? 0x25 : 0xC8;
            if (!exec(c, lba, n, n * 512, false)) return false;
            memcpy(dst, buf, n * 512);
            dst += n * 512;
            lba += n;
            count -= n;
        }
        return true;
    }

    bool write(uint64_t lba, uint32_t count, const void* in) override {
        auto* src = (const uint8_t*)in;
        while (count) {
            uint32_t n = min(count, MAX_SECTORS);
            memcpy(buf, src, n * 512);
            uint8_t c = lba48 ? 0x35 : 0xCA;
            if (!exec(c, lba, n, n * 512, true)) return false;
            src += n * 512;
            lba += n;
            count -= n;
        }
        return true;
    }

    bool flush() override { return exec(lba48 ? 0xEA : 0xE7, 0, 0, 0, false); }
};

int disk_count = 0;

struct Controller {
    volatile uint8_t* hba;
    uint32_t pi;
    uint32_t done = 0;            // puertos ya resueltos (disco añadido o lector ignorado)
    AhciDisk* ports[32] = {};     // estado de cada puerto entre intentos
    int attempts[32] = {};
};
Vec<Controller*> controllers;

void init_controller(const PciDevice& d) {
    pci::enable_bus_master(d);
    uint64_t abar = pci::bar_address(d, 5);
    if (!abar) { klog("AHCI: BAR5 no válido\n"); return; }
    auto* hba = (volatile uint8_t*)mm::map_mmio(abar, 0x1100);
    auto reg = [&](uint32_t off) -> volatile uint32_t& { return *(volatile uint32_t*)(hba + off); };

    // Traspaso BIOS -> sistema operativo
    if (reg(CAP2) & 1) {
        reg(BOHC) |= 2;
        wait_until(100, [&] { return !(reg(BOHC) & 1); });
        if (reg(BOHC) & 0x10) wait_until(2000, [&] { return !(reg(BOHC) & 0x10); });
    }
    reg(GHC) |= 0x80000000u; // AHCI habilitado
    reg(GHC) &= ~2u;         // sin interrupciones
    uint32_t cap = reg(CAP);
    uint32_t vs = reg(VS);
    auto* c = new Controller();
    c->hba = hba;
    c->pi = reg(PI);
    klog("AHCI %x.%x en %02x:%02x.%u (%04x:%04x): %d puertos, PI=%x, 64 bits: %s\n", vs >> 16, vs & 0xFFFF, d.bus, d.dev,
         d.func, d.vendor, d.device, (cap & 31) + 1, c->pi, (cap & (1u << 31)) ? "sí" : "no");
    controllers.push(c);
}

// Un intento sobre un puerto. Devuelve true si el puerto queda resuelto.
bool probe_port(Controller* c, int i) {
    volatile uint32_t* pr = (volatile uint32_t*)(c->hba + 0x100 + i * 0x80);
    uint32_t ssts = pr[P_SSTS / 4];
    uint8_t det = ssts & 0xF, ipm = (ssts >> 8) & 0xF;
    if (det != 3 || ipm == 0) return false;   // nada conectado (todavía)
    int n = ++c->attempts[i];
    AhciDisk* disk = c->ports[i];
    if (!disk) {
        disk = new AhciDisk();
        disk->hba = c->hba;
        disk->port = i;
        if (!disk->setup()) { klog("AHCI: puerto %d: no se pudo preparar\n", i); delete disk; return false; }
        c->ports[i] = disk;
    }
    if (!disk->wait_signature(500)) {
        klog("AHCI: puerto %d: firma no disponible (%x), COMRESET (intento %d)\n", i, disk->sig(), n);
        disk->comreset();
        if (!disk->wait_signature(2000)) return false;
    }
    uint32_t sig = disk->sig();
    if (sig == 0xEB140101) {
        klog("AHCI: puerto %d: lector de CD/DVD (no se usa)\n", i);
        return true;
    }
    if (sig != 0x00000101) {
        klog("AHCI: puerto %d: firma %x desconocida\n", i, sig);
        return n >= 3;
    }
    if (!disk->identify()) {
        klog("AHCI: puerto %d: IDENTIFY falló (intento %d)\n", i, n);
        if (n % 2 == 0) disk->comreset();
        return false;
    }
    ksnprintf(disk->name, sizeof disk->name, "SATA%d", disk_count++);
    klog("AHCI: puerto %d: \"%s\", %lu MiB\n", i, disk->model, disk->sectors / 2048);
    block::add_disk(disk);
    c->ports[i] = nullptr;
    return true;
}
}

void ahci::init() {
    for (auto& d : pci::devices())
        if (d.cls == 0x01 && d.subclass == 0x06 && d.prog_if == 0x01) init_controller(d);
    rescan();
}

void ahci::rescan() {
    for (Controller* c : controllers)
        for (int i = 0; i < 32; i++) {
            if (!(c->pi & (1u << i)) || (c->done & (1u << i))) continue;
            if (probe_port(c, i)) c->done |= 1u << i;
        }
}

bool ahci::present() { return !controllers.empty(); }

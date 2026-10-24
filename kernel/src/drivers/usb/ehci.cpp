// Controladora USB 2.0 EHCI (alta velocidad, 480 Mbit/s). Se queda con los dispositivos de
// alta velocidad (pendrives, discos externos) y cede los de velocidad completa o baja
// (teclados, ratones) a sus controladoras compañeras UHCI/OHCI.
//
// Sólo se usa la planificación asíncrona (control y bulk) con un único QH: se programa la
// transferencia con la planificación detenida, se activa, se espera a que termine y se
// vuelve a detener. Así nunca se modifica un QH que la controladora pueda tener en caché.
#include "drivers/usb/usb_internal.h"
#include "mm/memory.h"
#include "arch/idt.h"
#include "lib/log.h"
#include "lib/string.h"

namespace {
// Registros operativos
constexpr uint32_t USBCMD = 0x00, USBSTS = 0x04, USBINTR = 0x08, CTRLDSSEGMENT = 0x10, ASYNCLISTADDR = 0x18,
                   CONFIGFLAG = 0x40, PORTSC = 0x44;
constexpr uint32_t CMD_RUN = 1, CMD_RESET = 2, CMD_ASYNC = 1u << 5;
constexpr uint32_t STS_HALTED = 1u << 12, STS_ASYNC = 1u << 15;
// PORTSC
constexpr uint32_t P_CONNECT = 1, P_CSC = 2, P_ENABLE = 4, P_PEC = 8, P_OCC = 0x20, P_RESET = 1u << 8,
                   P_POWER = 1u << 12, P_OWNER = 1u << 13, P_W1C = P_CSC | P_PEC | P_OCC;
// Token de los qTD
constexpr uint32_t T_ACTIVE = 0x80, T_HALTED = 0x40, T_DBUF = 0x20, T_BABBLE = 0x10, T_XACT = 0x08;
constexpr uint32_t PID_OUT = 0, PID_IN = 1, PID_SETUP = 2;
constexpr uint32_t LINK_T = 1, LINK_QH = 2;

struct alignas(32) Qtd {
    volatile uint32_t next, alt, token, buf[5], buf_hi[5];
    uint32_t pad[3];
};
struct alignas(32) Qh {
    volatile uint32_t link, chars, caps, cur;
    volatile uint32_t next, alt, token, buf[5], buf_hi[5];
    uint32_t pad[7];
};
static_assert(sizeof(Qtd) == 64 && sizeof(Qh) == 96);

class Ehci : public usb::Controller {
public:
    static constexpr uint32_t BULK_MAX = 128 * 1024, TD_BYTES = 16384, NTDS = BULK_MAX / TD_BYTES + 4;
    volatile uint8_t* opregs = nullptr;
    int nports = 0;
    Qh* qh;
    uint32_t qh_phys;
    // Dos juegos de qTD que se alternan: así dos transferencias seguidas nunca usan los
    // mismos qTD (la emulación de QEMU da por hecho un qTD idéntico en la misma dirección)
    Qtd* tds_mem;
    uint32_t tds_mem_phys;
    Qtd* tds;
    uint32_t tds_phys;
    int bank = 0;
    Qtd* stop_td;           // qTD inactivo: tras un paquete corto la cola se detiene aquí
    uint32_t stop_phys;
    uint8_t* buf;           // búfer de rebote para los datos (por debajo de 4 GiB)
    uint32_t buf_phys;
    uint8_t* setup;
    uint32_t setup_phys;

    uint32_t rd(uint32_t r) { return *(volatile uint32_t*)(opregs + r); }
    void wr(uint32_t r, uint32_t v) { *(volatile uint32_t*)(opregs + r) = v; }
    uint32_t port(int p) { return PORTSC + (uint32_t)p * 4; }
    uint32_t td_phys(int i) { return tds_phys + (uint32_t)i * sizeof(Qtd); }

    void next_bank() {
        bank ^= 1;
        tds = tds_mem + bank * NTDS;
        tds_phys = tds_mem_phys + (uint32_t)(bank * NTDS) * sizeof(Qtd);
    }

    bool high_speed_only() const override { return true; }
    uint32_t max_bulk() const override { return BULK_MAX; }

    bool init(volatile uint8_t* cap) {
        uint8_t caplen = cap[0];
        uint32_t hcs = *(volatile uint32_t*)(cap + 4);
        uint32_t hcc = *(volatile uint32_t*)(cap + 8);
        opregs = cap + caplen;
        nports = (int)(hcs & 0xF);
        qh = (Qh*)usb::dma_alloc(sizeof(Qh), 32, qh_phys);
        tds_mem = (Qtd*)usb::dma_alloc(sizeof(Qtd) * NTDS * 2, 32, tds_mem_phys);
        tds = tds_mem;
        tds_phys = tds_mem_phys;
        stop_td = (Qtd*)usb::dma_alloc(sizeof(Qtd), 32, stop_phys);
        setup = (uint8_t*)usb::dma_alloc(64, 32, setup_phys);
        uint64_t bp = mm::alloc_pages(BULK_MAX / 4096);
        if (!qh || !tds_mem || !stop_td || !setup || !bp) return false;
        if (bp + BULK_MAX > 0x100000000ull) { mm::free_pages(bp, BULK_MAX / 4096); return false; }
        buf = (uint8_t*)mm::phys_to_virt(bp);
        buf_phys = (uint32_t)bp;
        stop_td->next = LINK_T;
        stop_td->alt = LINK_T;
        stop_td->token = 0;

        // detención y reinicio
        wr(USBCMD, rd(USBCMD) & ~CMD_RUN);
        for (int i = 0; i < 20 && !(rd(USBSTS) & STS_HALTED); i++) usb::sleep(1);
        wr(USBCMD, CMD_RESET);
        for (int i = 0; i < 100 && (rd(USBCMD) & CMD_RESET); i++) usb::sleep(1);
        if (rd(USBCMD) & CMD_RESET) { klog("EHCI: la controladora no sale del reinicio\n"); return false; }
        if (hcc & 1) wr(CTRLDSSEGMENT, 0);   // estructuras por debajo de 4 GiB
        wr(USBINTR, 0);
        wr(USBSTS, 0x3F);
        wr(ASYNCLISTADDR, qh_phys);
        wr(USBCMD, (8u << 16) | CMD_RUN);    // umbral de interrupción de 8 microtramas, en marcha
        for (int i = 0; i < 20 && (rd(USBSTS) & STS_HALTED); i++) usb::sleep(1);
        if (rd(USBSTS) & STS_HALTED) { klog("EHCI: la controladora no arranca\n"); return false; }
        wr(CONFIGFLAG, 1);                   // todos los puertos pasan a la EHCI
        usb::sleep(5);
        if (hcs & (1u << 4))                 // alimentación por puerto
            for (int p = 0; p < nports; p++) wr(port(p), (rd(port(p)) & ~P_W1C) | P_POWER);
        usb::sleep(100);   // los dispositivos tardan en notar la alimentación y conectarse
        return nports > 0;
    }

    // ------------------------------------------------------------------ puertos
    void release(int p) {
        uint32_t v = rd(port(p));
        wr(port(p), (v & ~P_W1C) | P_OWNER);
    }
    bool release_port(int p) override {
        if (p < 0 || p >= nports) return false;
        release(p);
        return true;
    }

    int port_count() override { return nports; }

    bool port_connected(int p) override {
        uint32_t v = rd(port(p));
        return (v & P_CONNECT) && !(v & P_OWNER);
    }

    bool port_changed(int p) override {
        uint32_t v = rd(port(p));
        if (v & P_OWNER) {
            // lo maneja una compañera; al desconectarse el propio hardware lo devuelve a la
            // EHCI (especificación EHCI 4.2.2)
            if (v & P_CSC) wr(port(p), (v & ~P_W1C) | P_CSC);
            return false;
        }
        if (!(v & P_CSC)) return false;
        wr(port(p), (v & ~P_W1C) | P_CSC);
        return true;
    }

    bool port_reset(int p, usb::Speed& speed) override {
        uint32_t v = rd(port(p));
        if (!(v & P_CONNECT) || (v & P_OWNER)) return false;
        if (((v >> 10) & 3) == 1) {          // estado K: baja velocidad, para la compañera
            release(p);
            return false;
        }
        wr(port(p), (v & ~P_W1C & ~P_ENABLE) | P_RESET);
        usb::sleep(50);
        wr(port(p), rd(port(p)) & ~P_W1C & ~P_RESET);
        for (int i = 0; i < 20 && (rd(port(p)) & P_RESET); i++) usb::sleep(1);
        usb::sleep(5);
        v = rd(port(p));
        if (!(v & P_CONNECT)) return false;
        if (!(v & P_ENABLE)) {               // no es de alta velocidad: para la compañera
            release(p);
            return false;
        }
        speed = usb::HIGH;
        return true;
    }

    // ------------------------------------------------------------ transferencias
    void fill(Qtd* t, uint32_t next, uint32_t alt, uint32_t pid, uint32_t len, uint32_t data_phys, bool toggle, bool ioc) {
        t->next = next;
        t->alt = alt;
        t->token = (len << 16) | (ioc ? 1u << 15 : 0) | (3u << 10) | (pid << 8) | T_ACTIVE | (toggle ? 1u << 31 : 0);
        for (int i = 0; i < 5; i++) {
            t->buf[i] = len ? ((data_phys & ~0xFFFu) + (uint32_t)i * 4096) : 0;
            t->buf_hi[i] = 0;
        }
        if (len) t->buf[0] = data_phys;
    }

    // Ejecuta la cola que empieza en el qTD 0 y espera a que acabe. Devuelve los bytes
    // transferidos en los qTD de datos [first, last], -1 si hubo error o -2 si STALL.
    int run(uint32_t chars, int first, int last, int count, uint8_t& toggle, uint32_t timeout_ms) {
        qh->link = qh_phys | LINK_QH;
        qh->chars = chars | (1u << 15);       // cabeza de la lista de reclamación
        qh->caps = 1u << 30;                  // una transacción por microtrama
        qh->cur = 0;
        qh->next = td_phys(0);
        qh->alt = LINK_T;
        qh->token = toggle ? 1u << 31 : 0;
        for (int i = 0; i < 5; i++) { qh->buf[i] = 0; qh->buf_hi[i] = 0; }
        asm volatile("mfence" ::: "memory");
        wr(ASYNCLISTADDR, qh_phys);
        wr(USBCMD, rd(USBCMD) | CMD_ASYNC);
        int result = -1;   // si se agota el tiempo
        uint64_t end = pit::ticks() + timeout_ms;
        while (pit::ticks() < end) {
            bool done = false, shortp = false;
            int total = 0, err = 0;
            for (int i = 0; i < count; i++) {
                uint32_t tok = tds[i].token;
                if (tok & T_HALTED) {
                    err = (tok & (T_BABBLE | T_XACT | T_DBUF)) ? -1 : -2;
                    done = true;
                    break;
                }
                if (tok & T_ACTIVE) {
                    if (shortp) done = true;   // la cola se detuvo tras un paquete corto
                    break;
                }
                if (i >= first && i <= last) {
                    uint32_t left = (tok >> 16) & 0x7FFF;
                    total += (int)(td_len[i] - left);
                    if (left) shortp = true;
                }
                if (i == count - 1) done = true;
            }
            if (done) {
                result = err ? err : total;
                break;
            }
            asm volatile("pause");
        }
        wr(USBCMD, rd(USBCMD) & ~CMD_ASYNC);
        for (int i = 0; i < 100 && (rd(USBSTS) & STS_ASYNC); i++) usb::sleep(1);
        toggle = (qh->token >> 31) & 1;
        return result;
    }
    uint32_t td_len[NTDS];

    bool control(usb::Device& d, const usb::SetupPacket& s, void* data) override {
        bool in = s.type & 0x80;
        uint32_t len = s.length;
        if (len > TD_BYTES) return false;
        next_bank();
        memcpy(setup + bank * 32, &s, 8);
        if (!in && len) memcpy(buf, data, len);
        int n = 0;
        int status = len ? 2 : 1;
        fill(&tds[n], td_phys(n + 1), LINK_T, PID_SETUP, 8, setup_phys + (uint32_t)bank * 32, false, false);
        td_len[n++] = 8;
        if (len) {
            // un paquete corto en la fase de datos salta directamente a la de estado
            fill(&tds[n], td_phys(n + 1), td_phys(status), in ? PID_IN : PID_OUT, len, buf_phys, true, false);
            td_len[n++] = len;
        }
        fill(&tds[n], LINK_T, LINK_T, (in && len) ? PID_OUT : PID_IN, 0, 0, true, true);
        td_len[n++] = 0;
        uint8_t tog = 0;
        uint32_t chars = d.address | (2u << 12) | (1u << 14) | ((uint32_t)d.mps0 << 16);
        int r = run(chars, 1, len ? 1 : 0, n, tog, 1000);
        if (r < 0 || (tds[n - 1].token & T_ACTIVE)) {
            klog("EHCI: control %02x/%02x a %u falló (%d): %08x %08x %08x qh %08x\n", s.type, s.request, d.address, r,
                 tds[0].token, tds[1].token, tds[n - 1].token, qh->token);
            return false;
        }
        if (in && len) memcpy(data, buf, len);
        return true;
    }

    int bulk(usb::Device& d, uint8_t ep, bool in, void* data, uint32_t len, uint8_t& toggle, uint16_t mps) override {
        if (len > BULK_MAX) return -1;
        next_bank();
        if (!in && len) memcpy(buf, data, len);
        int n = 0;
        uint32_t off = 0;
        do {
            uint32_t chunk = min(TD_BYTES, len - off);
            int next = n + 1;
            bool last = off + chunk >= len;
            fill(&tds[n], last ? LINK_T : td_phys(next), in ? stop_phys : LINK_T, in ? PID_IN : PID_OUT, chunk,
                 buf_phys + off, false, last);
            td_len[n] = chunk;
            off += chunk;
            n++;
        } while (off < len);
        uint32_t chars = d.address | ((uint32_t)ep << 8) | (2u << 12) | ((uint32_t)mps << 16);
        int r = run(chars, 0, n - 1, n, toggle, 5000);
        if (r > 0 && in) memcpy(data, buf, (size_t)r);
        return r;
    }

    // Los teclados y ratones (baja o completa velocidad) los atienden las compañeras
    void* open_interrupt(usb::Device&) override { return nullptr; }
    int poll_interrupt(void*, uint8_t*, int) override { return -1; }
    void close_interrupt(void*) override {}
};

// Pide a la BIOS que suelte la controladora (USBLEGSUP en el espacio PCI)
void bios_handoff(const PciDevice& d, volatile uint8_t* cap) {
    uint32_t hccparams = *(volatile uint32_t*)(cap + 8);
    uint8_t eecp = (hccparams >> 8) & 0xFF;
    for (int guard = 0; eecp >= 0x40 && guard < 16; guard++) {
        uint32_t c = pci::read32(d, eecp);
        if ((c & 0xFF) == 1) {   // USBLEGSUP
            pci::write8(d, (uint8_t)(eecp + 3), 1);   // el sistema operativo lo solicita
            if (c & (1u << 16)) {
                for (int i = 0; i < 1000 && (pci::read32(d, eecp) & (1u << 16)); i++) usb::sleep(1);
                if (pci::read32(d, eecp) & (1u << 16)) klog("EHCI: la BIOS no libera la controladora\n");
            }
            pci::write32(d, eecp + 4, 0);   // sin SMI heredados
            break;
        }
        eecp = (c >> 8) & 0xFF;
    }
}
}

usb::Controller* usb::ehci_create(const PciDevice& d) {
    uint64_t bar = pci::bar_address(d, 0);
    if (!bar) return nullptr;
    pci::enable_bus_master(d);
    auto* cap = (volatile uint8_t*)mm::map_mmio(bar, 0x1000);
    bios_handoff(d, cap);
    auto* e = new Ehci();
    e->kind = "EHCI";
    if (!e->init(cap)) {
        // sin USB 2.0: los puertos se ceden a las compañeras, como antes
        auto* op = (volatile uint32_t*)(cap + cap[0]);
        op[0] &= ~1u;
        op[CONFIGFLAG / 4] = 0;
        klog("USB: EHCI %04x:%04x no disponible; puertos cedidos a las compañeras\n", d.vendor, d.device);
        delete e;
        return nullptr;
    }
    klog("USB: EHCI (USB 2.0) en %02x:%02x.%u, %d puertos\n", d.bus, d.dev, d.func, e->nports);
    return e;
}

// Controladora USB 1.1 UHCI (Intel/VIA). Es la que usan los puertos USB de baja y
// velocidad completa del chipset ICH9M del HP Pavilion dv5.
#include "drivers/usb/usb_internal.h"
#include "arch/io.h"
#include "arch/idt.h"
#include "lib/log.h"
#include "lib/string.h"

namespace {
constexpr uint16_t CMD = 0x00, STS = 0x02, INTR = 0x04, FRNUM = 0x06, FRBASE = 0x08, SOFMOD = 0x0C, PORTSC = 0x10;
// Errores de un TD: STALL (22), búfer (21), babble (20), CRC/tiempo agotado (18) y bitstuff (17).
// El bit 19 (NAK recibido) NO es un error: el dispositivo pidió que se repitiera la transacción,
// y las controladoras reales lo dejan marcado aunque el TD termine bien (los pendrives
// responden NAK continuamente mientras leen la memoria flash).
constexpr uint32_t TD_ACTIVE = 1u << 23, TD_STALL = 1u << 22, TD_ERRMASK = 0x76u << 16;
constexpr uint32_t TD_LS = 1u << 26, TD_IOC = 1u << 24, TD_CERR3 = 3u << 27, TD_SPD = 1u << 29;
constexpr uint8_t PID_SETUP = 0x2D, PID_IN = 0x69, PID_OUT = 0xE1;
constexpr uint32_t LINK_T = 1, LINK_Q = 2, LINK_VF = 4;

struct alignas(16) Td {
    volatile uint32_t link, status, token, buffer;
    uint32_t pad[4];
};
struct alignas(16) Qh {
    volatile uint32_t head, element;
    uint32_t pad[2];
};

struct Pipe {
    Qh* qh;
    uint32_t qh_phys;
    Td* td;
    uint32_t td_phys;
    uint8_t* buf;
    uint32_t buf_phys;
    uint8_t toggle = 0;
    usb::Device* dev;
    bool active = true;
};

class Uhci : public usb::Controller {
public:
    uint16_t io;
    uint32_t* frames;
    Qh* int_head;
    uint32_t int_head_phys;
    Qh* ctrl_qh;
    uint32_t ctrl_phys;
    Td* tds;
    uint32_t tds_phys;
    uint8_t* xbuf;   // búfer para la fase de datos y el paquete SETUP
    uint32_t xbuf_phys;
    int nports = 2;
    // transferencias bulk (almacenamiento USB)
    static constexpr uint32_t BULK_MAX = 32768, BULK_TDS = 513;   // hasta 512 paquetes por transferencia
    Qh* bulk_qh;
    uint32_t bulk_qh_phys;
    Td* btds;
    uint32_t btds_phys;
    uint8_t* bbuf;
    uint32_t bbuf_phys;

    int bulk(usb::Device& d, uint8_t ep, bool in, void* data, uint32_t len, uint8_t& toggle, uint16_t mps) override {
        if (len > BULK_MAX || !mps || len > (BULK_TDS - 1) * mps) return -1;
        bool ls = d.speed == usb::LOW;
        if (!in && len) memcpy(bbuf, data, len);
        int n = 0;
        uint32_t off = 0;
        do {
            uint16_t chunk = (uint16_t)min<uint32_t>(mps, len - off);
            fill_td(&btds[n], 0, ls, in ? PID_IN : PID_OUT, d.address, ep, (toggle + n) & 1, chunk, bbuf_phys + off);
            if (in) btds[n].status |= TD_SPD;   // un paquete corto termina la transferencia
            off += chunk;
            n++;
        } while (off < len && n < (int)BULK_TDS);
        for (int i = 0; i < n; i++)
            btds[i].link = i + 1 < n ? ((btds_phys + (uint32_t)(i + 1) * sizeof(Td)) | LINK_VF) : LINK_T;
        btds[n - 1].status |= TD_IOC;
        asm volatile("mfence" ::: "memory");
        bulk_qh->element = btds_phys;
        int result = -1;
        uint64_t end = pit::ticks() + 5000;
        while (pit::ticks() < end) {
            int total = 0, done_tds = 0;
            bool finished = true, err = false, stall = false, shortp = false;
            for (int i = 0; i < n; i++) {
                uint32_t st = btds[i].status;
                if (st & TD_ACTIVE) { finished = false; break; }
                if (st & TD_ERRMASK) { err = true; stall = st & TD_STALL; break; }
                uint32_t actual = (st + 1) & 0x7FF;
                uint32_t maxlen = ((btds[i].token >> 21) + 1) & 0x7FF;
                total += (int)actual;
                done_tds++;
                if (actual < maxlen) { shortp = true; break; }
            }
            if (err || shortp || finished) {
                toggle = (uint8_t)((toggle + done_tds) & 1);
                result = err ? (stall ? -2 : -1) : total;
                break;
            }
            asm volatile("pause");
        }
        bulk_qh->element = LINK_T;
        if (result > 0 && in) memcpy(data, bbuf, (size_t)result);
        return result;
    }

    uint32_t phys_of(const void* p, const void* base, uint32_t base_phys) {
        return base_phys + (uint32_t)((const uint8_t*)p - (const uint8_t*)base);
    }

    bool init() {
        uint32_t fp;
        frames = (uint32_t*)usb::dma_alloc(4096, 4096, fp);
        int_head = (Qh*)usb::dma_alloc(sizeof(Qh), 16, int_head_phys);
        ctrl_qh = (Qh*)usb::dma_alloc(sizeof(Qh), 16, ctrl_phys);
        tds = (Td*)usb::dma_alloc(sizeof(Td) * 64, 16, tds_phys);
        xbuf = (uint8_t*)usb::dma_alloc(1024 + 8, 16, xbuf_phys);
        bulk_qh = (Qh*)usb::dma_alloc(sizeof(Qh), 16, bulk_qh_phys);
        btds = (Td*)usb::dma_alloc(sizeof(Td) * BULK_TDS, 16, btds_phys);
        bbuf = (uint8_t*)usb::dma_alloc(BULK_MAX, 16, bbuf_phys);
        if (!frames || !int_head || !ctrl_qh || !tds || !xbuf || !bulk_qh || !btds || !bbuf) return false;
        bulk_qh->head = LINK_T;
        bulk_qh->element = LINK_T;
        ctrl_qh->head = bulk_qh_phys | LINK_Q;
        ctrl_qh->element = LINK_T;
        int_head->head = ctrl_phys | LINK_Q;
        int_head->element = LINK_T;
        for (int i = 0; i < 1024; i++) frames[i] = int_head_phys | LINK_Q;

        // reinicio global y de la controladora
        outw(io + CMD, 0x0004);
        usb::sleep(15);
        outw(io + CMD, 0);
        usb::sleep(5);
        outw(io + CMD, 0x0002);
        for (int i = 0; i < 50 && (inw(io + CMD) & 2); i++) usb::sleep(1);
        outw(io + INTR, 0);
        outw(io + STS, 0xFFFF);
        outl(io + FRBASE, fp);
        outw(io + FRNUM, 0);
        outb(io + SOFMOD, 0x40);
        outw(io + CMD, 0x0001 | 0x0040 | 0x0080);   // en marcha, configurada, paquetes de 64 bytes
        usb::sleep(5);
        if (inw(io + STS) & 0x20) { klog("UHCI: la controladora no arranca\n"); return false; }
        // los puertos existentes tienen el bit 7 siempre a 1
        nports = 0;
        for (int p = 0; p < 8; p++) {
            uint16_t v = inw(io + PORTSC + p * 2);
            if (v == 0xFFFF || !(v & 0x80)) break;
            nports++;
        }
        return nports > 0;
    }

    int port_count() override { return nports; }
    uint16_t port_reg(int p) { return (uint16_t)(io + PORTSC + p * 2); }
    bool port_connected(int p) override { return inw(port_reg(p)) & 1; }

    bool port_changed(int p) override {
        uint16_t v = inw(port_reg(p));
        if (!(v & 0x0002)) return false;
        outw(port_reg(p), (uint16_t)((v & ~0x000Au) | 0x0002));   // borra CSC
        return true;
    }

    bool port_reset(int p, usb::Speed& speed) override {
        uint16_t r = port_reg(p);
        outw(r, (uint16_t)((inw(r) & ~0x000Au) | 0x0200));
        usb::sleep(58);
        outw(r, (uint16_t)(inw(r) & ~0x020Au));
        usb::sleep(10);
        for (int i = 0; i < 16; i++) {
            uint16_t v = inw(r);
            if (!(v & 1)) return false;
            if (v & 0x000A) { outw(r, (uint16_t)((v & ~0x000Au) | (v & 0x000A))); continue; }
            if (v & 4) {
                speed = (v & 0x0100) ? usb::LOW : usb::FULL;
                return true;
            }
            outw(r, (uint16_t)((v & ~0x000Au) | 4));
            usb::sleep(10);
        }
        return false;
    }

    void fill_td(Td* t, uint32_t link, bool ls, uint8_t pid, uint8_t addr, uint8_t ep, int toggle, uint16_t len, uint32_t buf) {
        t->link = link;
        t->status = TD_ACTIVE | TD_CERR3 | (ls ? TD_LS : 0);
        uint32_t maxlen = len ? (uint32_t)(len - 1) : 0x7FF;
        t->token = pid | ((uint32_t)addr << 8) | ((uint32_t)ep << 15) | ((uint32_t)toggle << 19) | (maxlen << 21);
        t->buffer = buf;
    }

    bool control(usb::Device& d, const usb::SetupPacket& s, void* data) override {
        bool in = s.type & 0x80;
        uint16_t len = s.length;
        if (len > 1024) return false;
        bool ls = d.speed == usb::LOW;
        memcpy(xbuf + 1024, &s, 8);
        if (!in && len) memcpy(xbuf, data, len);
        int n = 0;
        fill_td(&tds[n], 0, ls, PID_SETUP, d.address, 0, 0, 8, xbuf_phys + 1024);
        n++;
        int toggle = 1;
        for (uint16_t off = 0; off < len; off += d.mps0) {
            uint16_t chunk = min<uint16_t>(d.mps0, len - off);
            fill_td(&tds[n], 0, ls, in ? PID_IN : PID_OUT, d.address, 0, toggle, chunk, xbuf_phys + off);
            toggle ^= 1;
            n++;
            if (n >= 62) return false;
        }
        fill_td(&tds[n], LINK_T, ls, in ? PID_OUT : PID_IN, d.address, 0, 1, 0, 0);
        tds[n].status |= TD_IOC;
        for (int i = 0; i < n; i++) tds[i].link = (tds_phys + (uint32_t)(i + 1) * sizeof(Td)) | LINK_VF;
        asm volatile("mfence" ::: "memory");
        ctrl_qh->element = tds_phys;
        bool ok = false;
        uint64_t end = pit::ticks() + 1000;
        while (pit::ticks() < end) {
            uint32_t st = tds[n].status;
            if (!(st & TD_ACTIVE)) { ok = !(st & TD_ERRMASK); break; }
            bool err = false;
            for (int i = 0; i <= n; i++)
                if (!(tds[i].status & TD_ACTIVE) && (tds[i].status & TD_ERRMASK)) err = true;
            if (err) break;
            asm volatile("pause");
        }
        ctrl_qh->element = LINK_T;
        if (ok && in && len) memcpy(data, xbuf, len);
        return ok;
    }

    void arm(Pipe* p) {
        fill_td(p->td, LINK_T, p->dev->speed == usb::LOW, PID_IN, p->dev->address, p->dev->ep, p->toggle,
                p->dev->ep_mps, p->buf_phys);
        asm volatile("mfence" ::: "memory");
        p->qh->element = p->td_phys;
    }

    void* open_interrupt(usb::Device& d) override {
        auto* p = new Pipe();
        p->dev = &d;
        p->qh = (Qh*)usb::dma_alloc(sizeof(Qh), 16, p->qh_phys);
        p->td = (Td*)usb::dma_alloc(sizeof(Td), 16, p->td_phys);
        p->buf = (uint8_t*)usb::dma_alloc(64, 16, p->buf_phys);
        if (!p->qh || !p->td || !p->buf) { delete p; return nullptr; }
        p->qh->element = LINK_T;
        // se inserta justo después de la cabecera de interrupciones
        p->qh->head = int_head->head;
        arm(p);
        asm volatile("mfence" ::: "memory");
        int_head->head = p->qh_phys | LINK_Q;
        return p;
    }

    int poll_interrupt(void* pp, uint8_t* out, int max) override {
        auto* p = (Pipe*)pp;
        if (!p->active) return -1;
        uint32_t st = p->td->status;
        if (st & TD_ACTIVE) return -1;
        int n = -1;
        if (!(st & TD_ERRMASK)) {
            uint32_t actual = (st + 1) & 0x7FF;
            n = min<int>((int)actual, max);
            memcpy(out, p->buf, (size_t)n);
            p->toggle ^= 1;
        }
        arm(p);
        return n;
    }

    void close_interrupt(void* pp) override {
        auto* p = (Pipe*)pp;
        p->active = false;
        p->qh->element = LINK_T;   // la cola queda vacía en el planificador
    }
};
}

usb::Controller* usb::uhci_create(const PciDevice& d) {
    uint32_t io = pci::bar_io(d, 4);
    if (!io) return nullptr;
    uint16_t cmd = pci::read16(d, 0x04);
    pci::write16(d, 0x04, (uint16_t)(cmd | 0x05));   // E/S + bus master
    pci::write16(d, 0xC0, 0x8F00);                    // retira la emulación heredada (SMI) de la BIOS
    auto* u = new Uhci();
    u->kind = "UHCI";
    u->io = (uint16_t)io;
    if (!u->init()) { delete u; return nullptr; }
    klog("USB: UHCI en %02x:%02x.%u, E/S %x, %d puertos\n", d.bus, d.dev, d.func, io, u->nports);
    return u;
}

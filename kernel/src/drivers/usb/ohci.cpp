// Controladora USB 1.1 OHCI (VirtualBox, chipsets AMD/SiS/NVIDIA)
#include "drivers/usb/usb_internal.h"
#include "mm/memory.h"
#include "arch/idt.h"
#include "lib/log.h"
#include "lib/string.h"

namespace {
constexpr uint32_t HcRevision = 0x00, HcControl = 0x04, HcCommandStatus = 0x08, HcInterruptStatus = 0x0C,
                   HcInterruptDisable = 0x14, HcHCCA = 0x18, HcControlHeadED = 0x20, HcControlCurrentED = 0x24,
                   HcBulkHeadED = 0x28, HcFmInterval = 0x34, HcPeriodicStart = 0x40, HcLSThreshold = 0x44,
                   HcRhDescriptorA = 0x48, HcRhStatus = 0x50, HcRhPortStatus = 0x54;
constexpr uint32_t CTL_IR = 1u << 8, CTL_PLE = 1u << 2, CTL_CLE = 1u << 4;
constexpr uint32_t ED_SKIP = 1u << 14, ED_LS = 1u << 13;
constexpr uint32_t TD_DP_SETUP = 0u << 19, TD_DP_OUT = 1u << 19, TD_DP_IN = 2u << 19;
constexpr uint32_t TD_R = 1u << 18, TD_DI_NONE = 7u << 21, TD_CC_NA = 0xFu << 28;
constexpr uint32_t TD_T_DATA0 = 2u << 24, TD_T_DATA1 = 3u << 24;

struct alignas(16) Ed {
    volatile uint32_t ctrl, tail, head, next;
};
struct alignas(16) Td {
    volatile uint32_t ctrl, cbp, next, be;
};

struct Pipe {
    Ed* ed;
    uint32_t ed_phys;
    Td* td;
    uint32_t td_phys;
    Td* dummy;
    uint32_t dummy_phys;
    uint8_t* buf;
    uint32_t buf_phys;
    usb::Device* dev;
    bool active = true;
};

class Ohci : public usb::Controller {
public:
    volatile uint8_t* regs;
    uint32_t* hcca;
    Ed* int_head;
    uint32_t int_head_phys;
    Ed* ctrl_ed;
    uint32_t ctrl_ed_phys;
    Td* tds;
    uint32_t tds_phys;
    uint8_t* xbuf;
    uint32_t xbuf_phys;
    int nports = 0;
    // transferencias bulk (almacenamiento USB): un TD por página de 4 KiB
    static constexpr uint32_t BULK_MAX = 32768;
    Ed* bulk_ed;
    uint32_t bulk_ed_phys;
    Td* btds;
    uint32_t btds_phys;
    uint8_t* bbuf;
    uint32_t bbuf_phys;

    int bulk(usb::Device& d, uint8_t ep, bool in, void* data, uint32_t len, uint8_t& toggle, uint16_t mps) override {
        if (len > BULK_MAX || !len) return -1;
        if (!in) memcpy(bbuf, data, len);
        bulk_ed->ctrl = ED_SKIP;
        int n = 0;
        for (uint32_t off = 0; off < len; off += 4096, n++) {
            uint32_t chunk = min<uint32_t>(4096, len - off);
            // T = 0x: el bit de alternancia lo lleva el ED. Sin redondeo (R) en los TD
            // intermedios: un paquete corto detiene el ED (DataUnderrun) en vez de pasar al
            // siguiente TD y robarle al dispositivo el paquete de estado.
            bool last = off + chunk >= len;
            fill_td(&btds[n], in ? (TD_DP_IN | (last ? TD_R : 0)) : TD_DP_OUT, bbuf_phys + off, (uint16_t)chunk, 0);
        }
        fill_td(&btds[n], 0, 0, 0, 0);   // TD vacío de cola
        for (int i = 0; i < n; i++) btds[i].next = btds_phys + (uint32_t)(i + 1) * sizeof(Td);
        uint32_t tail = btds_phys + (uint32_t)n * sizeof(Td);
        bulk_ed->tail = tail;
        bulk_ed->head = btds_phys | (toggle ? 2u : 0u);
        bulk_ed->next = 0;
        asm volatile("mfence" ::: "memory");
        bulk_ed->ctrl = (uint32_t)d.address | ((uint32_t)ep << 7) | (d.speed == usb::LOW ? ED_LS : 0) | ((uint32_t)mps << 16);
        wr(HcCommandStatus, 1u << 2);   // lista bulk con trabajo
        int result = -1;
        uint64_t end = pit::ticks() + 5000;
        while (pit::ticks() < end) {
            uint32_t head = bulk_ed->head;
            if ((head & ~0xFu) != tail && !(head & 1)) { asm volatile("pause"); continue; }
            // terminada o detenida: se suman los bytes de cada TD
            int total = 0;
            for (int i = 0; i < n; i++) {
                uint32_t cc = btds[i].ctrl >> 28;
                if (cc == 0xF) break;   // no se llegó a procesar
                if (cc != 0 && cc != 9) { total = cc == 4 ? -2 : -1; break; }
                uint32_t want = min<uint32_t>(4096, len - (uint32_t)i * 4096);
                uint32_t cbp = btds[i].cbp;
                uint32_t got = cbp ? cbp - (bbuf_phys + (uint32_t)i * 4096) : want;
                total += (int)got;
                if (got < want) break;   // paquete corto: fin de los datos
            }
            result = total;
            break;
        }
        bulk_ed->ctrl |= ED_SKIP;
        usb::sleep(1);
        toggle = (bulk_ed->head & 2) ? 1 : 0;
        bulk_ed->head = 0;
        bulk_ed->tail = 0;
        wr(HcInterruptStatus, 1u << 1);
        if (result > 0 && in) memcpy(data, bbuf, (size_t)result);
        return result;
    }

    uint32_t rd(uint32_t r) { return *(volatile uint32_t*)(regs + r); }
    void wr(uint32_t r, uint32_t v) { *(volatile uint32_t*)(regs + r) = v; }

    bool init() {
        uint32_t hcca_phys;
        hcca = (uint32_t*)usb::dma_alloc(256, 256, hcca_phys);
        int_head = (Ed*)usb::dma_alloc(sizeof(Ed), 16, int_head_phys);
        ctrl_ed = (Ed*)usb::dma_alloc(sizeof(Ed), 16, ctrl_ed_phys);
        tds = (Td*)usb::dma_alloc(sizeof(Td) * 64, 16, tds_phys);
        xbuf = (uint8_t*)usb::dma_alloc(1024 + 8, 16, xbuf_phys);
        bulk_ed = (Ed*)usb::dma_alloc(sizeof(Ed), 16, bulk_ed_phys);
        btds = (Td*)usb::dma_alloc(sizeof(Td) * 10, 16, btds_phys);
        bbuf = (uint8_t*)usb::dma_alloc(BULK_MAX, 4096, bbuf_phys);
        if (!hcca || !int_head || !ctrl_ed || !tds || !xbuf || !bulk_ed || !btds || !bbuf) return false;
        bulk_ed->ctrl = ED_SKIP;
        int_head->ctrl = ED_SKIP;
        int_head->next = 0;
        ctrl_ed->ctrl = ED_SKIP;
        for (int i = 0; i < 32; i++) hcca[i] = int_head_phys;

        // Traspaso desde la BIOS (SMM) si la controladora está en uso
        uint32_t ctl = rd(HcControl);
        if (ctl & CTL_IR) {
            wr(HcCommandStatus, 1u << 3);   // petición de cambio de propietario
            for (int i = 0; i < 1000 && (rd(HcControl) & CTL_IR); i++) usb::sleep(1);
            if (rd(HcControl) & CTL_IR) klog("OHCI: la BIOS no libera la controladora\n");
        } else if ((ctl & 0xC0) != 0) {
            usb::sleep(20);
        }
        uint32_t fi = rd(HcFmInterval) & 0x3FFF;
        if (fi < 0x2000) fi = 0x2EDF;
        wr(HcCommandStatus, 1);   // reinicio
        for (int i = 0; i < 100 && (rd(HcCommandStatus) & 1); i++) usb::sleep(1);
        wr(HcHCCA, hcca_phys);
        wr(HcControlHeadED, ctrl_ed_phys);
        wr(HcControlCurrentED, 0);
        wr(HcBulkHeadED, bulk_ed_phys);
        wr(HcInterruptDisable, 0xC000007F);
        wr(HcInterruptStatus, 0x7F);
        uint32_t fsmps = ((fi - 210) * 6) / 7;
        wr(HcFmInterval, ((rd(HcFmInterval) & 0x80000000u) ^ 0x80000000u) | (fsmps << 16) | fi);
        wr(HcPeriodicStart, fi * 9 / 10);
        wr(HcLSThreshold, 0x628);
        wr(HcControl, (2u << 6) | CTL_PLE | CTL_CLE | (1u << 5) | 3);   // operativa (con lista bulk)
        // Alimentación de los puertos
        uint32_t ra = rd(HcRhDescriptorA);
        nports = ra & 0xFF;
        if (nports > 15) nports = 15;
        if (!(ra & (1u << 9))) {
            if (ra & (1u << 8)) for (int p = 0; p < nports; p++) wr(HcRhPortStatus + p * 4, 1u << 8);
            else wr(HcRhStatus, 1u << 16);
        }
        usb::sleep(((ra >> 24) & 0xFF) * 2 + 20);
        return nports > 0;
    }

    int port_count() override { return nports; }
    bool port_connected(int p) override { return rd(HcRhPortStatus + p * 4) & 1; }
    bool port_changed(int p) override {
        uint32_t v = rd(HcRhPortStatus + p * 4);
        if (!(v & (1u << 16))) return false;
        wr(HcRhPortStatus + p * 4, 1u << 16);
        return true;
    }

    bool port_reset(int p, usb::Speed& speed) override {
        uint32_t r = HcRhPortStatus + p * 4;
        wr(r, 1u << 4);   // SetPortReset
        for (int i = 0; i < 100 && !(rd(r) & (1u << 20)); i++) usb::sleep(1);
        wr(r, 1u << 20);  // borra PRSC
        usb::sleep(10);
        uint32_t v = rd(r);
        if (!(v & 1)) return false;
        if (!(v & 2)) { wr(r, 2); usb::sleep(5); v = rd(r); }   // SetPortEnable
        speed = (v & (1u << 9)) ? usb::LOW : usb::FULL;
        wr(r, (1u << 16) | (1u << 17));
        return (v & 2) != 0;
    }

    void fill_td(Td* t, uint32_t flags, uint32_t buf, uint16_t len, uint32_t next) {
        t->ctrl = TD_CC_NA | TD_DI_NONE | flags;
        t->cbp = len ? buf : 0;
        t->be = len ? buf + len - 1 : 0;
        t->next = next;
    }

    bool control(usb::Device& d, const usb::SetupPacket& s, void* data) override {
        bool in = s.type & 0x80;
        uint16_t len = s.length;
        if (len > 1024) return false;
        memcpy(xbuf + 1024, &s, 8);
        if (!in && len) memcpy(xbuf, data, len);
        ctrl_ed->ctrl = ED_SKIP;
        int n = 0;
        auto tdp = [&](int i) { return tds_phys + (uint32_t)i * sizeof(Td); };
        fill_td(&tds[n], TD_DP_SETUP | TD_T_DATA0, xbuf_phys + 1024, 8, 0);
        n++;
        bool data1 = true;
        for (uint16_t off = 0; off < len; off += d.mps0) {
            uint16_t chunk = min<uint16_t>(d.mps0, len - off);
            fill_td(&tds[n], (in ? TD_DP_IN | TD_R : TD_DP_OUT) | (data1 ? TD_T_DATA1 : TD_T_DATA0), xbuf_phys + off, chunk, 0);
            data1 = !data1;
            n++;
            if (n >= 61) return false;
        }
        int last = n;
        fill_td(&tds[n], (in ? TD_DP_OUT : TD_DP_IN) | TD_T_DATA1, 0, 0, 0);
        n++;
        // TD vacío de cola
        fill_td(&tds[n], 0, 0, 0, 0);
        for (int i = 0; i < n; i++) tds[i].next = tdp(i + 1);
        ctrl_ed->tail = tdp(n);
        ctrl_ed->head = tdp(0);
        ctrl_ed->next = 0;
        asm volatile("mfence" ::: "memory");
        ctrl_ed->ctrl = (uint32_t)d.address | (d.speed == usb::LOW ? ED_LS : 0) | ((uint32_t)d.mps0 << 16);
        wr(HcCommandStatus, 1u << 1);   // lista de control con trabajo
        bool ok = false;
        uint64_t end = pit::ticks() + 1000;
        while (pit::ticks() < end) {
            uint32_t cc = tds[last].ctrl >> 28;
            if (cc != 0xF) { ok = cc == 0; break; }
            if (ctrl_ed->head & 1) break;   // detenido por error
            asm volatile("pause");
        }
        ctrl_ed->ctrl |= ED_SKIP;
        usb::sleep(1);
        ctrl_ed->head = 0;
        ctrl_ed->tail = 0;
        wr(HcInterruptStatus, 1u << 1);   // WDH
        if (ok && in && len) memcpy(data, xbuf, len);
        return ok;
    }

    void arm(Pipe* p, bool keep_toggle) {
        p->ed->ctrl |= ED_SKIP;
        fill_td(p->td, TD_DP_IN | TD_R, p->buf_phys, p->dev->ep_mps, p->dummy_phys);
        fill_td(p->dummy, 0, 0, 0, 0);
        p->ed->tail = p->dummy_phys;
        uint32_t carry = keep_toggle ? (p->ed->head & 2) : 0;
        p->ed->head = p->td_phys | carry;
        asm volatile("mfence" ::: "memory");
        p->ed->ctrl = (uint32_t)p->dev->address | ((uint32_t)p->dev->ep << 7) | (2u << 11) |
                      (p->dev->speed == usb::LOW ? ED_LS : 0) | ((uint32_t)p->dev->ep_mps << 16);
    }

    void* open_interrupt(usb::Device& d) override {
        auto* p = new Pipe();
        p->dev = &d;
        p->ed = (Ed*)usb::dma_alloc(sizeof(Ed), 16, p->ed_phys);
        p->td = (Td*)usb::dma_alloc(sizeof(Td), 16, p->td_phys);
        p->dummy = (Td*)usb::dma_alloc(sizeof(Td), 16, p->dummy_phys);
        p->buf = (uint8_t*)usb::dma_alloc(64, 16, p->buf_phys);
        if (!p->ed || !p->td || !p->dummy || !p->buf) { delete p; return nullptr; }
        p->ed->ctrl = ED_SKIP;
        arm(p, false);
        p->ed->next = int_head->next;
        asm volatile("mfence" ::: "memory");
        int_head->next = p->ed_phys;
        return p;
    }

    int poll_interrupt(void* pp, uint8_t* out, int max) override {
        auto* p = (Pipe*)pp;
        if (!p->active) return -1;
        uint32_t cc = p->td->ctrl >> 28;
        if (cc == 0xF && !(p->ed->head & 1)) return -1;
        wr(HcInterruptStatus, 1u << 1);
        int n = -1;
        if (cc == 0 || cc == 9) {   // sin error o paquete corto
            uint32_t cbp = p->td->cbp;
            uint32_t got = cbp ? cbp - p->buf_phys : p->dev->ep_mps;
            n = min<int>((int)got, max);
            memcpy(out, p->buf, (size_t)n);
        }
        arm(p, true);
        return n;
    }

    void close_interrupt(void* pp) override {
        auto* p = (Pipe*)pp;
        p->active = false;
        p->ed->ctrl |= ED_SKIP;
    }
};
}

usb::Controller* usb::ohci_create(const PciDevice& d) {
    uint64_t bar = pci::bar_address(d, 0);
    if (!bar) return nullptr;
    pci::enable_bus_master(d);
    auto* o = new Ohci();
    o->kind = "OHCI";
    o->regs = (volatile uint8_t*)mm::map_mmio(bar, 0x1000);
    if (!o->init()) { delete o; return nullptr; }
    klog("USB: OHCI %x en %02x:%02x.%u, %d puertos\n", o->rd(HcRevision) & 0xFF, d.bus, d.dev, d.func, o->nports);
    return o;
}

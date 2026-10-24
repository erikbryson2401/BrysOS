// Núcleo USB: detección de controladoras, enumeración de dispositivos en los puertos
// raíz y sondeo de las transferencias de interrupción (teclado y ratón HID).
#include "drivers/usb.h"
#include "drivers/usb/usb_internal.h"
#include "drivers/pci.h"
#include "arch/idt.h"
#include "mm/memory.h"
#include "lib/log.h"
#include "lib/string.h"
#include "lib/vec.h"
#include "drivers/keyboard.h"

namespace {
Vec<usb::Controller*> controllers;
Vec<usb::Device*> devices;
uint64_t next_port_check = 0;
volatile bool leds_dirty = false;
volatile bool leds_caps = false;

// Llamado desde el decodificador (incluso en la IRQ del PS/2): sólo anota el cambio
void led_hook(bool caps) {
    leds_caps = caps;
    leds_dirty = true;
}

uint8_t* pool = nullptr;
uint32_t pool_phys = 0;
size_t pool_used = 0, pool_size = 0;

uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

// Retira un dispositivo y, si es un concentrador, todo lo que cuelga de él
void remove_tree(usb::Device* d) {
    for (size_t i = devices.size(); i > 0; i--) {
        if (i - 1 >= devices.size()) continue;
        if (devices[i - 1]->parent == d) remove_tree(devices[i - 1]);
    }
    if (d->pipe) d->hc->close_interrupt(d->pipe);
    if (d->storage) usb::msd_detach(*d);
    devices.remove(d);
    delete d;
}

bool get_descriptor(usb::Device& d, uint8_t type, uint8_t index, void* buf, uint16_t len) {
    usb::SetupPacket s{0x80, 6, (uint16_t)((type << 8) | index), 0, len};
    return d.hc->control(d, s, buf);
}

void attach(usb::Controller* hc, int root_port, usb::Device* parent, int hub_port, usb::Speed sp);

bool hub_request(usb::Device& h, uint8_t req, uint16_t feature, uint16_t port) {
    usb::SetupPacket s{0x23, req, feature, port, 0};
    return h.hc->control(h, s, nullptr);
}

bool hub_port_status(usb::Device& h, int port, uint16_t& status, uint16_t& change) {
    uint8_t b[4];
    usb::SetupPacket s{0xA3, 0, 0, (uint16_t)port, 4};
    if (!h.hc->control(h, s, b)) return false;
    status = rd16(b);
    change = rd16(b + 2);
    return true;
}

// Reinicia un puerto de un concentrador y devuelve la velocidad del dispositivo
bool hub_port_reset(usb::Device& h, int port, usb::Speed& sp) {
    if (!hub_request(h, 3, 4, (uint16_t)port)) return false;   // SET_FEATURE(PORT_RESET)
    uint16_t st = 0, ch = 0;
    for (int i = 0; i < 50; i++) {
        usb::sleep(10);
        if (!hub_port_status(h, port, st, ch)) return false;
        if (ch & 0x10) break;
    }
    hub_request(h, 1, 20, (uint16_t)port);                       // CLEAR_FEATURE(C_PORT_RESET)
    if (!(st & 1) || !(st & 2)) return false;
    sp = (st & 0x200) ? usb::LOW : ((st & 0x400) ? usb::HIGH : usb::FULL);
    usb::sleep(10);
    return true;
}

void hub_scan(usb::Device* h, bool only_changes) {
    for (int p = 1; p <= h->hub_ports; p++) {
        uint16_t st, ch;
        if (!hub_port_status(*h, p, st, ch)) continue;
        if (only_changes && !(ch & 1)) continue;
        if (ch & 1) hub_request(*h, 1, 16, (uint16_t)p);   // CLEAR_FEATURE(C_PORT_CONNECTION)
        // retira lo que hubiera en ese puerto
        for (size_t i = devices.size(); i > 0; i--) {
            if (i - 1 >= devices.size()) continue;
            usb::Device* d = devices[i - 1];
            if (d->parent == h && d->hub_port == p) remove_tree(d);
        }
        if (st & 1) {
            usb::sleep(100);
            usb::Speed sp;
            if (hub_port_reset(*h, p, sp)) {
                // un USB 1.1 detrás de un concentrador USB 2.0 necesita transacciones divididas
                if (h->hc->high_speed_only() && sp != usb::HIGH)
                    klog("USB: dispositivo USB 1.1 en el puerto %d de un concentrador USB 2.0 (no compatible); "
                         "conéctelo directamente al equipo\n", p);
                else attach(h->hc, h->port, h, p, sp);
            }
        }
    }
}

void attach(usb::Controller* hc, int root_port, usb::Device* parent, int hub_port, usb::Speed sp) {
    auto* d = new usb::Device();
    d->hc = hc;
    d->port = root_port;
    d->parent = parent;
    d->hub_port = hub_port;
    d->speed = sp;
    d->mps0 = 8;
    const char* where = parent ? "concentrador" : "puerto";
    int wp = parent ? hub_port : root_port;
    uint8_t dd[18] = {};
    // primeros 8 bytes del descriptor de dispositivo en la dirección 0
    if (!get_descriptor(*d, 1, 0, dd, 8)) {
        klog("USB %s %s %d: el dispositivo no responde\n", hc->kind, where, wp);
        delete d;
        return;
    }
    d->mps0 = dd[7] ? dd[7] : 8;
    uint8_t addr = hc->next_address++;
    if (hc->next_address > 127) hc->next_address = 1;
    usb::SetupPacket sa{0x00, 5, addr, 0, 0};
    if (!hc->control(*d, sa, nullptr)) { klog("USB: no se pudo asignar la dirección\n"); delete d; return; }
    usb::sleep(10);
    d->address = addr;
    if (!get_descriptor(*d, 1, 0, dd, 18)) { delete d; return; }
    d->vendor = rd16(dd + 8);
    d->product = rd16(dd + 10);
    uint8_t cfg_head[9];
    if (!get_descriptor(*d, 2, 0, cfg_head, 9)) { delete d; return; }
    uint16_t total = min<uint16_t>(rd16(cfg_head + 2), 512);
    uint8_t* cfg = (uint8_t*)mm::kcalloc(total);
    if (!get_descriptor(*d, 2, 0, cfg, total)) { mm::kfree(cfg); delete d; return; }
    uint8_t cfg_value = cfg[5];
    // Busca una interfaz HID de arranque con un punto final de interrupción de entrada
    int cur_iface = -1, cur_class = 0, cur_sub = 0, cur_proto = 0;
    int msd_iface = -1;
    uint8_t msd_in = 0, msd_out = 0;
    uint16_t msd_in_mps = 0, msd_out_mps = 0;
    for (uint16_t off = 0; off + 2 <= total && cfg[off]; off += cfg[off]) {
        uint8_t len = cfg[off], type = cfg[off + 1];
        if (type == 4 && len >= 9) {
            cur_iface = cfg[off + 2];
            cur_class = cfg[off + 5];
            cur_sub = cfg[off + 6];
            cur_proto = cfg[off + 7];
        } else if (type == 5 && len >= 7 && !d->kind && cur_class == 3 && cur_sub == 1 && (cur_proto == 1 || cur_proto == 2)) {
            uint8_t ep = cfg[off + 2], attr = cfg[off + 3];
            if ((ep & 0x80) && (attr & 3) == 3) {
                d->kind = cur_proto;
                d->iface = (uint8_t)cur_iface;
                d->ep = ep & 0x0F;
                d->ep_mps = rd16(cfg + off + 4) & 0x7FF;
                d->interval = cfg[off + 6];
            }
        } else if (type == 5 && len >= 7 && cur_class == 8 && cur_sub == 6 && cur_proto == 0x50 &&
                   (msd_iface < 0 || msd_iface == cur_iface)) {
            // almacenamiento masivo: un punto final bulk de entrada y otro de salida
            uint8_t ep = cfg[off + 2], attr = cfg[off + 3];
            if ((attr & 3) == 2) {
                msd_iface = cur_iface;
                if (ep & 0x80) { msd_in = ep & 0x0F; msd_in_mps = rd16(cfg + off + 4) & 0x7FF; }
                else { msd_out = ep & 0x0F; msd_out_mps = rd16(cfg + off + 4) & 0x7FF; }
            }
        }
    }
    mm::kfree(cfg);
    usb::SetupPacket sc{0x00, 9, cfg_value, 0, 0};
    if (dd[4] == 9) {
        // Concentrador: se alimentan sus puertos y se exploran
        if (!hc->control(*d, sc, nullptr)) { delete d; return; }
        uint8_t hd[16] = {};
        usb::SetupPacket sh{0xA0, 6, 0x2900, 0, 8};
        if (!hc->control(*d, sh, hd)) { delete d; return; }
        d->is_hub = true;
        d->hub_ports = hd[2];
        ksnprintf(d->desc, sizeof d->desc, "Concentrador USB %04x:%04x", d->vendor, d->product);
        klog("USB %s %s %d: concentrador con %d puertos\n", hc->kind, where, wp, d->hub_ports);
        devices.push(d);
        for (int p = 1; p <= d->hub_ports; p++) hub_request(*d, 3, 8, (uint16_t)p);   // SET_FEATURE(PORT_POWER)
        usb::sleep((uint32_t)max(100, hd[5] * 2));
        hub_scan(d, false);
        return;
    }
    if (!d->kind && msd_iface >= 0 && msd_in && msd_out) {
        if (!hc->control(*d, sc, nullptr)) { delete d; return; }
        usb::sleep(20);
        devices.push(d);
        if (!usb::msd_attach(*d, (uint8_t)msd_iface, msd_in, msd_in_mps, msd_out, msd_out_mps))
            ksnprintf(d->desc, sizeof d->desc, "Almacenamiento USB %04x:%04x (sin medio)", d->vendor, d->product);
        return;
    }
    if (!d->kind) {
        klog("USB %s %s %d: dispositivo %04x:%04x sin controlador\n", hc->kind, where, wp, d->vendor, d->product);
        ksnprintf(d->desc, sizeof d->desc, "Dispositivo USB %04x:%04x", d->vendor, d->product);
        devices.push(d);   // se recuerda para no volver a enumerarlo
        return;
    }
    if (hc->high_speed_only()) {
        // La EHCI sólo hace transferencias de control y bulk: un teclado o ratón USB 2.0
        // se pasa a la controladora compañera, donde funciona a velocidad completa
        if (!parent && hc->release_port(root_port))
            klog("USB %s %s %d: %04x:%04x es un teclado o ratón; se cede a la controladora compañera\n", hc->kind,
                 where, wp, d->vendor, d->product);
        delete d;
        return;
    }
    usb::SetupPacket sp_boot{0x21, 0x0B, 0, d->iface, 0};   // SET_PROTOCOL: arranque
    usb::SetupPacket si{0x21, 0x0A, 0, d->iface, 0};        // SET_IDLE: sólo informar cambios
    if (!hc->control(*d, sc, nullptr)) { delete d; return; }
    hc->control(*d, sp_boot, nullptr);
    hc->control(*d, si, nullptr);
    d->pipe = hc->open_interrupt(*d);
    if (!d->pipe) { delete d; return; }
    ksnprintf(d->desc, sizeof d->desc, "%s USB %04x:%04x", d->kind == 1 ? "Teclado" : "Ratón", d->vendor, d->product);
    klog("USB %s %s %d: %s (%s)\n", hc->kind, where, wp, d->desc, sp == usb::LOW ? "baja velocidad" : "velocidad completa");
    devices.push(d);
}

void enumerate(usb::Controller* hc, int port) {
    usb::Speed sp;
    if (hc->port_reset(port, sp)) attach(hc, port, nullptr, 0, sp);
}

void check_ports() {
    for (usb::Controller* hc : controllers) {
        for (int p = 0; p < hc->port_count(); p++) {
            if (!hc->port_changed(p)) continue;
            // desconexión: se olvida todo lo que colgaba de ese puerto
            for (size_t i = devices.size(); i > 0; i--) {
                if (i - 1 >= devices.size()) continue;
                usb::Device* d = devices[i - 1];
                if (d->hc == hc && d->port == p && !d->parent) {
                    klog("USB %s puerto %d: dispositivo retirado\n", hc->kind, p);
                    remove_tree(d);
                }
            }
            if (hc->port_connected(p)) {
                usb::sleep(100);   // estabilización eléctrica
                enumerate(hc, p);
            }
        }
    }
    // cambios en los puertos de los concentradores
    for (size_t i = 0; i < devices.size(); i++)
        if (devices[i]->is_hub) hub_scan(devices[i], true);
}
}

void* usb::dma_alloc(size_t size, size_t align, uint32_t& phys) {
    size_t off = (pool_used + align - 1) & ~(align - 1);
    if (!pool || off + size > pool_size) {
        // bloque nuevo de 64 KiB contiguos
        uint64_t p = mm::alloc_pages(16);
        if (!p || p + 65536 > 0x100000000ull) { phys = 0; return nullptr; }
        pool = (uint8_t*)mm::phys_to_virt(p);
        pool_phys = (uint32_t)p;
        pool_size = 65536;
        pool_used = 0;
        off = 0;
    }
    pool_used = off + size;
    phys = pool_phys + (uint32_t)off;
    memset(pool + off, 0, size);
    return pool + off;
}

void usb::sleep(uint32_t ms) { pit::sleep_ms(ms); }
void usb::add_controller(Controller* c) { controllers.push(c); }

void usb::init() {
    // Primero las EHCI (USB 2.0): se quedan los puertos y ceden a las compañeras UHCI/OHCI
    // lo que no sea de alta velocidad
    int ehcis = 0;
    for (auto& d : pci::devices())
        if (d.cls == 0x0C && d.subclass == 0x03 && d.prog_if == 0x20)
            if (Controller* c = ehci_create(d)) { add_controller(c); ehcis++; }
    for (auto& d : pci::devices()) {
        if (d.cls != 0x0C || d.subclass != 0x03) continue;
        Controller* c = nullptr;
        if (d.prog_if == 0x00) c = uhci_create(d);
        else if (d.prog_if == 0x10) c = ohci_create(d);
        else if (d.prog_if == 0x30) klog("USB: controladora xHCI %04x:%04x (no compatible; se deja al firmware)\n", d.vendor, d.device);
        if (c) add_controller(c);
    }
    // Enumeración inicial: primero las EHCI, que ceden los puertos de baja y completa
    // velocidad; las compañeras los ven poco después
    for (int pass = 0; pass < 2; pass++) {
        for (Controller* hc : controllers) {
            if (hc->high_speed_only() != (pass == 0)) continue;
            for (int p = 0; p < hc->port_count(); p++) {
                hc->port_changed(p);
                if (hc->port_connected(p)) enumerate(hc, p);
            }
        }
        if (pass == 0 && ehcis) usb::sleep(100);
    }
    next_port_check = pit::ticks() + 500;
    if (!controllers.empty()) keyboard::set_led_hook(led_hook);
}

void usb::poll() {
    uint8_t buf[64];
    for (Device* d : devices) {
        if (!d->pipe) continue;
        int n = d->hc->poll_interrupt(d->pipe, buf, sizeof buf);
        if (n > 0) hid_report(*d, buf, n);
    }
    hid_tick();
    if (leds_dirty) {
        leds_dirty = false;
        hid_set_leds(leds_caps);
    }
    if (pit::ticks() >= next_port_check && !controllers.empty()) {
        next_port_check = pit::ticks() + 500;
        check_ports();
    }
}

int usb::device_count() { return (int)devices.size(); }
const char* usb::describe(int i) { return devices[(size_t)i]->desc[0] ? devices[(size_t)i]->desc : "Dispositivo USB"; }

void usb::hid_set_leds(bool caps) {
    uint8_t v = caps ? 2 : 0;
    for (Device* d : devices) {
        if (d->kind != 1) continue;
        SetupPacket s{0x21, 0x09, 0x0200, d->iface, 1};   // SET_REPORT de salida
        d->hc->control(*d, s, &v);
    }
}

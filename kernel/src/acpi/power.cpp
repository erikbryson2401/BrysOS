// Energía: el intérprete de AML con el hardware real (memoria, puertos, PCI, EC), la batería,
// el cargador y el brillo de la pantalla
#include "acpi/power.h"
#include "acpi/aml.h"
#include "acpi/ec.h"
#include "drivers/acpi.h"
#include "drivers/pci.h"
#include "arch/io.h"
#include "arch/idt.h"
#include "mm/memory.h"
#include "lib/log.h"
#include "lib/string.h"
#include "lib/vec.h"

namespace {
// ------------------------------------------------------------------ hardware para el AML
uint64_t hw_read(uint8_t space, uint64_t a, int bits) {
    switch (space) {
    case aml::MEMORY: {
        auto* p = (volatile uint8_t*)mm::map_mmio(a, (size_t)bits / 8);
        switch (bits) {
        case 8: return *p;
        case 16: return *(volatile uint16_t*)p;
        case 32: return *(volatile uint32_t*)p;
        default: return *(volatile uint64_t*)p;
        }
    }
    case aml::IO:
        if (a > 0xFFFF) return 0;
        switch (bits) {
        case 8: return inb((uint16_t)a);
        case 16: return inw((uint16_t)a);
        case 32: return inl((uint16_t)a);
        default: return inl((uint16_t)a) | ((uint64_t)inl((uint16_t)(a + 4)) << 32);
        }
    case aml::EC: {
        uint8_t v;
        ec::read((uint8_t)a, v);
        return v;
    }
    case aml::CMOS:
        outb(0x70, (uint8_t)a);
        return inb(0x71);
    }
    return 0;
}

void hw_write(uint8_t space, uint64_t a, int bits, uint64_t v) {
    switch (space) {
    case aml::MEMORY: {
        auto* p = (volatile uint8_t*)mm::map_mmio(a, (size_t)bits / 8);
        switch (bits) {
        case 8: *p = (uint8_t)v; break;
        case 16: *(volatile uint16_t*)p = (uint16_t)v; break;
        case 32: *(volatile uint32_t*)p = (uint32_t)v; break;
        default: *(volatile uint64_t*)p = v; break;
        }
        return;
    }
    case aml::IO:
        if (a > 0xFFFF) return;
        switch (bits) {
        case 8: outb((uint16_t)a, (uint8_t)v); break;
        case 16: outw((uint16_t)a, (uint16_t)v); break;
        case 32: outl((uint16_t)a, (uint32_t)v); break;
        default: outl((uint16_t)a, (uint32_t)v); outl((uint16_t)(a + 4), (uint32_t)(v >> 32)); break;
        }
        return;
    case aml::EC: ec::write((uint8_t)a, (uint8_t)v); return;
    case aml::CMOS:
        outb(0x70, (uint8_t)a);
        outb(0x71, (uint8_t)v);
        return;
    }
}

PciDevice pdev(uint32_t bus, uint32_t dev, uint32_t fn) {
    PciDevice d{};
    d.bus = (uint8_t)bus;
    d.dev = (uint8_t)dev;
    d.func = (uint8_t)fn;
    return d;
}

uint64_t hw_pci_read(uint32_t seg, uint32_t bus, uint32_t dev, uint32_t fn, uint32_t off, int bits) {
    if (seg || off > 255) return ~0ull;
    uint32_t v = pci::read32(pdev(bus, dev, fn), (uint8_t)(off & ~3u));
    v >>= (off & 3) * 8;
    if (bits == 8) return v & 0xFF;
    if (bits == 16) return v & 0xFFFF;
    if (bits == 64) return v | ((uint64_t)pci::read32(pdev(bus, dev, fn), (uint8_t)(off + 4)) << 32);
    return v;
}

void hw_pci_write(uint32_t seg, uint32_t bus, uint32_t dev, uint32_t fn, uint32_t off, int bits, uint64_t v) {
    if (seg || off > 255) return;
    PciDevice d = pdev(bus, dev, fn);
    if (bits == 8) pci::write8(d, (uint8_t)off, (uint8_t)v);
    else if (bits == 16) pci::write16(d, (uint8_t)off, (uint16_t)v);
    else {
        pci::write32(d, (uint8_t)off, (uint32_t)v);
        if (bits == 64) pci::write32(d, (uint8_t)(off + 4), (uint32_t)(v >> 32));
    }
}

void hw_sleep(uint32_t ms) { pit::sleep_ms(ms); }
void hw_stall(uint32_t us) {
    for (uint32_t i = 0; i < us; i++) io_wait();   // ~1 µs cada una
}
uint64_t hw_timer() { return pit::ticks() * 10000; }

volatile bool notified = false;   // el firmware avisa de un cambio (batería, cargador...)
void hw_notify(aml::Node*, uint64_t) { notified = true; }

// Como Windows Vista/7, la época del HP dv5: así el firmware se comporta como lo probó el
// fabricante
bool hw_osi(const char* s) {
    static const char* const yes[] = {"Windows 2000", "Windows 2001", "Windows 2001 SP1", "Windows 2001.1",
                                      "Windows 2001 SP2", "Windows 2001.1 SP1", "Windows 2006", "Windows 2006 SP1",
                                      "Windows 2006.1", "Windows 2009", "Module Device", "Processor Device",
                                      "3.0 Thermal Model", nullptr};
    for (int i = 0; yes[i]; i++)
        if (!strcmp(s, yes[i])) return true;
    return false;
}

// ------------------------------------------------------------------ estado
bool loaded = false;
aml::Node* bat = nullptr;
aml::Node* acad = nullptr;
aml::Node* lcd = nullptr;           // salida de vídeo con _BCM y _BCL
power::Battery info;
bool ac_online = true;
uint64_t bif_at = 0;
int levels[64];
int nlevels = 0;
int current = -1;

uint32_t u32(aml::Object* pkg, uint32_t i) {
    uint64_t v;
    if (!aml::pkg_int(pkg, i, v)) return 0xFFFFFFFF;
    return (uint32_t)v;
}
bool known(uint32_t v) { return v != 0xFFFFFFFF && v < 0x7FFFFFFF; }

void copy_str(char* d, size_t cap, const char* s) {
    size_t i = 0;
    while (s[i] == ' ') s++;
    for (; s[i] && i + 1 < cap; i++) d[i] = s[i];
    d[i] = 0;
    while (i && d[i - 1] == ' ') d[--i] = 0;
}

void read_bif() {
    bif_at = pit::ticks();
    aml::Object* p = aml::evaluate(bat, "_BIX");
    int o = 1;   // _BIX lleva primero la revisión
    if (!p || p->type != aml::PACKAGE || p->package.count < 20) {
        aml::unref(p);
        p = aml::evaluate(bat, "_BIF");
        o = 0;
        if (!p || p->type != aml::PACKAGE || p->package.count < 13) { aml::unref(p); return; }
    }
    info.mwh = u32(p, o + 0) == 0;
    info.design = u32(p, o + 1);
    info.full = u32(p, o + 2);
    int s = o ? 16 : 9;
    copy_str(info.model, sizeof info.model, aml::pkg_string(p, (uint32_t)s));
    copy_str(info.type, sizeof info.type, aml::pkg_string(p, (uint32_t)s + 2));
    copy_str(info.oem, sizeof info.oem, aml::pkg_string(p, (uint32_t)s + 3));
    aml::unref(p);
}

void read_bst() {
    aml::Object* p = aml::evaluate(bat, "_BST");
    if (!p || p->type != aml::PACKAGE || p->package.count < 4) { aml::unref(p); return; }
    uint32_t st = u32(p, 0);
    info.discharging = known(st) && (st & 1);
    info.charging = known(st) && (st & 2);
    info.critical = known(st) && (st & 4);
    info.rate = u32(p, 1);
    info.remaining = u32(p, 2);
    info.voltage = u32(p, 3);
    aml::unref(p);
    uint32_t full = known(info.full) && info.full ? info.full : info.design;
    if (known(info.remaining) && known(full) && full) info.percent = (int)min<uint64_t>(100, (uint64_t)info.remaining * 100 / full);
    else info.percent = -1;
    info.minutes = -1;
    if (known(info.rate) && info.rate && known(info.remaining)) {
        if (info.discharging) info.minutes = (int)((uint64_t)info.remaining * 60 / info.rate);
        else if (info.charging && known(full) && full > info.remaining)
            info.minutes = (int)((uint64_t)(full - info.remaining) * 60 / info.rate);
    }
}

// ¿Está el adaptador gráfico (o el dispositivo) en el bus PCI? (los firmwares describen
// también las tarjetas de otras versiones del modelo)
bool on_pci(aml::Node* dev) {
    aml::Node* chain[16];
    int k = 0;
    aml::Node* rootb = nullptr;
    for (aml::Node* x = dev; x && x != aml::root() && k < 16; x = aml::parent(x)) {
        if (aml::device_is(x, "PNP0A03") || aml::device_is(x, "PNP0A08")) { rootb = x; break; }
        chain[k++] = x;
    }
    if (!rootb) return true;
    uint64_t bus = 0;
    aml::eval_int(rootb, "_BBN", bus);
    for (int i = k - 1; i >= 0; i--) {
        uint64_t adr;
        if (!aml::eval_int(chain[i], "_ADR", adr)) return false;
        uint32_t d = (uint32_t)(adr >> 16) & 31, fn = (uint32_t)adr & 7;
        if ((hw_pci_read(0, (uint32_t)bus, d, fn, 0, 16) & 0xFFFF) == 0xFFFF) return false;
        if (i && (hw_pci_read(0, (uint32_t)bus, d, fn, 0x0E, 8) & 0x7F) == 1) bus = hw_pci_read(0, (uint32_t)bus, d, fn, 0x19, 8);
    }
    return true;
}

struct Found {
    aml::Node* bat;
    aml::Node* acad;
    aml::Node* lcd;
    aml::Node* ec;
};

bool find_cb(aml::Node* n, void* c) {
    auto* f = (Found*)c;
    aml::Object* o = aml::node_object(n);
    if (!o) return true;
    if (o->type == aml::DEVICE) {
        if (!f->bat && aml::device_is(n, "PNP0C0A")) f->bat = n;
        else if (!f->acad && aml::device_is(n, "ACPI0003")) f->acad = n;
        else if (!f->ec && aml::device_is(n, "PNP0C09")) f->ec = n;
        if (!f->lcd && aml::exists(n, "_BCM") && aml::exists(n, "_BCL") && on_pci(aml::parent(n)))
            f->lcd = n;
    }
    return true;
}

// Puertos del EC en su _CRS (descriptores de E/S): datos y órdenes
void start_ec(aml::Node* dev) {
    uint16_t ports[2] = {0x62, 0x66};
    aml::Object* crs = aml::evaluate(dev, "_CRS");
    if (crs && crs->type == aml::BUFFER) {
        int k = 0;
        const uint8_t* b = crs->buffer.ptr;
        for (uint32_t i = 0; i < crs->buffer.len && k < 2;) {
            uint8_t tag = b[i];
            if (tag & 0x80) {   // descriptor grande
                if (i + 3 > crs->buffer.len) break;
                i += 3 + (uint32_t)(b[i + 1] | (b[i + 2] << 8));
                continue;
            }
            uint8_t item = (tag >> 3) & 0x0F, len = tag & 7;
            if (item == 0x0F) break;   // fin
            if (item == 0x08 && len >= 7 && i + 4 < crs->buffer.len) ports[k++] = (uint16_t)(b[i + 2] | (b[i + 3] << 8));
            if (item == 0x09 && len >= 3 && i + 2 < crs->buffer.len) ports[k++] = (uint16_t)(b[i + 1] | ((b[i + 2] & 3) << 8));
            i += 1 + len;
        }
    }
    aml::unref(crs);
    ec::init(ports[0], ports[1]);
}

void read_levels() {
    nlevels = 0;
    aml::Object* p = aml::evaluate(lcd, "_BCL");
    if (!p || p->type != aml::PACKAGE) { aml::unref(p); return; }
    // los dos primeros son los niveles por defecto con corriente y con batería
    for (uint32_t i = 2; i < p->package.count && nlevels < 64; i++) {
        uint64_t v;
        if (!aml::pkg_int(p, i, v) || v > 100) continue;
        bool dup = false;
        for (int j = 0; j < nlevels; j++) dup |= levels[j] == (int)v;
        if (!dup) levels[nlevels++] = (int)v;
    }
    aml::unref(p);
    for (int i = 1; i < nlevels; i++)
        for (int j = i; j > 0 && levels[j - 1] > levels[j]; j--) {
            int t = levels[j];
            levels[j] = levels[j - 1];
            levels[j - 1] = t;
        }
}
}

void power::init() {
    aml::Host h{hw_read, hw_write, hw_pci_read, hw_pci_write, hw_sleep, hw_stall, hw_timer, hw_notify, hw_osi};
    aml::init(h);
    // primero la DSDT (fija el tamaño de los enteros) y después las SSDT
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < acpi::table_count(); i++) {
            char sig[5];
            const uint8_t* d;
            uint32_t len;
            if (!acpi::table_at(i, sig, d, len)) continue;
            if ((pass == 0 && !strcmp(sig, "DSDT")) || (pass == 1 && !strcmp(sig, "SSDT"))) {
                bool ok = aml::load_table(d, len);
                if (!ok) klog("AML: %s con errores\n", sig);
            }
        }
    auto st = aml::stats();
    if (!st.tables) {
        klog("AML: no hay tablas\n");
        return;
    }
    loaded = true;
    Found f{};
    aml::walk(aml::root(), find_cb, &f);
    if (f.ec) {
        start_ec(f.ec);
        aml::run_reg(aml::EC);
    }
    aml::init_devices();
    bat = f.bat;
    acad = f.acad;
    lcd = f.lcd;
    if (lcd) read_levels();
    poll();
    st = aml::stats();
    char lp[96] = "-";
    if (lcd) aml::node_path(lcd, lp, sizeof lp);
    klog("AML: %u tablas, %u nombres, %u métodos, enteros de %u bits, %u errores; batería %s, cargador %s, brillo %s (%d niveles)\n",
         st.tables, st.nodes, st.methods, st.int_bits, st.errors, bat ? "sí" : "no", acad ? "sí" : "no", lp, nlevels);
}

void power::poll() {
    if (!loaded) return;
    notified = false;
    if (acad) {
        uint64_t v;
        if (aml::eval_int(acad, "_PSR", v)) ac_online = v != 0;
    }
    if (!bat) return;
    uint64_t sta = 0x1F;
    if (aml::exists(bat, "_STA") && !aml::eval_int(bat, "_STA", sta)) sta = 0;
    bool present = (sta & 0x10) != 0;
    if (!present) {
        info = Battery();
        return;
    }
    // la información fija se lee al aparecer la batería y después cada minuto
    if (!info.present || pit::ticks() - bif_at > 60000) read_bif();
    info.present = true;
    read_bst();
    if (!acad) ac_online = !info.discharging;
}

bool power::has_battery() { return info.present; }
bool power::on_ac() { return ac_online; }
const power::Battery& power::battery() { return info; }

bool power::has_brightness() { return lcd && nlevels > 1; }

int power::brightness_levels(int* out, int max) {
    int n = nlevels < max ? nlevels : max;
    for (int i = 0; i < n; i++) out[i] = levels[i];
    return n;
}

int power::brightness() {
    if (!lcd) return -1;
    if (aml::exists(lcd, "_BQC")) {
        uint64_t v;
        if (aml::eval_int(lcd, "_BQC", v) && v <= 100) {
            current = (int)v;
            return current;
        }
    }
    return current;
}

bool power::set_brightness(int pct) {
    if (!has_brightness()) return false;
    int best = levels[0];
    for (int i = 0; i < nlevels; i++)
        if (abs_val(levels[i] - pct) < abs_val(best - pct)) best = levels[i];
    aml::Object* a = aml::make_int((uint64_t)best);
    aml::Object* r = aml::evaluate(lcd, "_BCM", &a, 1);
    bool ok = r != nullptr;
    aml::unref(r);
    aml::unref(a);
    if (ok) current = best;
    return ok;
}

void power::diagnostics(String& out) {
    if (!loaded) { out += "AML: no se ha cargado ninguna tabla\r\n"; return; }
    auto st = aml::stats();
    out += String::format("Intérprete AML: %u tablas, %u nombres, %u métodos, enteros de %u bits, %u errores\r\n",
                          st.tables, st.nodes, st.methods, st.int_bits, st.errors);
    if (st.errors) out += String::format("Último error: %s\r\n", aml::last_error());
    out += String::format("Controlador empotrado: %s (%u esperas sin respuesta)\r\n", ec::present() ? "sí" : "no", ec::timeouts());
    char p[96];
    if (acad) {
        aml::node_path(acad, p, sizeof p);
        out += String::format("Cargador %s: %s\r\n", p, ac_online ? "conectado" : "desconectado");
    } else out += "Cargador: no se ha encontrado\r\n";
    if (bat) {
        aml::node_path(bat, p, sizeof p);
        const Battery& b = info;
        out += String::format("Batería %s: %s\r\n", p, b.present ? "presente" : "no está puesta");
        if (b.present) {
            const char* u = b.mwh ? "mWh" : "mAh";
            out += String::format("  carga %d %%, %s%s%s\r\n", b.percent, b.charging ? "cargando" : "", b.discharging ? "descargando" : "",
                                  b.critical ? " (nivel crítico)" : "");
            out += String::format("  restante %u %s, capacidad %u %s (diseño %u %s)\r\n", b.remaining, u, b.full, u, b.design, u);
            out += String::format("  consumo %u %s, tensión %u mV, tiempo %d min\r\n", b.rate, b.mwh ? "mW" : "mA", b.voltage, b.minutes);
            out += String::format("  modelo \"%s\", tipo \"%s\", fabricante \"%s\"\r\n", b.model, b.type, b.oem);
        }
    } else out += "Batería: no se ha encontrado\r\n";
    if (lcd) {
        aml::node_path(lcd, p, sizeof p);
        out += String::format("Brillo %s: actual %d %%, niveles:", p, brightness());
        for (int i = 0; i < nlevels; i++) out += String::format(" %d", levels[i]);
        out += "\r\n";
    } else out += "Brillo: el firmware no lo permite controlar\r\n";
}

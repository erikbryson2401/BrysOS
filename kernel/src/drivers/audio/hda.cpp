// Intel High Definition Audio: controladora (CORB/RIRB y flujo de salida con BDL) y
// configuración del códec (ruta desde un DAC hasta los altavoces y auriculares).
#include "drivers/audio.h"
#include "drivers/pci.h"
#include "mm/memory.h"
#include "arch/idt.h"
#include "lib/log.h"
#include "lib/string.h"
#include "lib/vec.h"

namespace {
volatile uint8_t* regs = nullptr;
int codec = -1;
bool immediate = false;   // interfaz de órdenes inmediatas si CORB/RIRB falla
uint32_t* corb = nullptr;
uint32_t* rirb = nullptr;
uint32_t corb_entries = 256, rirb_rp = 0;
uint32_t sd = 0;          // desplazamiento del descriptor del flujo de salida
char dev_name[64] = "HD Audio";
uint32_t codec_vid = 0, codec_ssid = 0;
int gpio_count = 0;
bool gpio_amp = false;
int out_pins[8];
int n_out_pins = 0;

uint8_t r8(uint32_t o) { return regs[o]; }
uint16_t r16(uint32_t o) { return *(volatile uint16_t*)(regs + o); }
uint32_t r32(uint32_t o) { return *(volatile uint32_t*)(regs + o); }
void w8(uint32_t o, uint8_t v) { regs[o] = v; }
void w16(uint32_t o, uint16_t v) { *(volatile uint16_t*)(regs + o) = v; }
void w32(uint32_t o, uint32_t v) { *(volatile uint32_t*)(regs + o) = v; }

bool wait_for(uint32_t ms, auto cond) {
    uint64_t end = pit::ticks() + ms;
    while (pit::ticks() < end) {
        if (cond()) return true;
        asm volatile("pause");
    }
    return cond();
}

uint32_t send_immediate(uint32_t v) {
    if (!wait_for(10, [] { return !(r16(0x68) & 1); })) return 0xFFFFFFFF;
    w32(0x60, v);
    w16(0x68, (uint16_t)(r16(0x68) | 1));
    if (!wait_for(10, [] { return (r16(0x68) & 2) != 0; })) return 0xFFFFFFFF;
    uint32_t resp = r32(0x64);
    w16(0x68, 2);
    return resp;
}

uint32_t send(uint32_t v) {
    if (immediate) return send_immediate(v);
    uint16_t wp = (uint16_t)((r16(0x48) + 1) % corb_entries);
    corb[wp] = v;
    asm volatile("mfence" ::: "memory");
    w16(0x48, wp);
    uint32_t want = (rirb_rp + 1) % corb_entries;
    if (!wait_for(20, [&] { return (r16(0x58) & 0xFF) == want; })) {
        // algunas controladoras no responden por CORB/RIRB: se usa la interfaz inmediata
        immediate = true;
        klog("HDA: CORB/RIRB no responde; se usan órdenes inmediatas\n");
        return send_immediate(v);
    }
    rirb_rp = want;
    return rirb[want * 2];
}

uint32_t verb(int nid, uint32_t v) { return send(((uint32_t)codec << 28) | ((uint32_t)nid << 20) | (v & 0xFFFFF)); }
uint32_t param(int nid, int p) { return verb(nid, 0xF0000 | (uint32_t)p); }

// --- Grafo del códec
constexpr int MAXN = 128;
uint8_t wtype[MAXN];
uint32_t wcaps[MAXN];
uint8_t conns[MAXN][16];
uint8_t nconns[MAXN];
int afg = -1;
uint32_t afg_out_amp = 0;

void read_conns(int n) {
    uint32_t len = param(n, 0x0E);
    bool lng = len & 0x80;
    int count = (int)(len & 0x7F);
    nconns[n] = 0;
    for (int i = 0; i < count && nconns[n] < 16;) {
        uint32_t e = verb(n, 0xF0200 | (uint32_t)i);
        int per = lng ? 2 : 4;
        for (int k = 0; k < per && i < count && nconns[n] < 16; k++, i++) {
            uint32_t id = lng ? (e >> (16 * k)) & 0xFFFF : (e >> (8 * k)) & 0xFF;
            // los rangos (bit 7/15) se tratan como entradas sueltas
            conns[n][nconns[n]++] = (uint8_t)(id & (lng ? 0x7FFF : 0x7F));
        }
    }
}

// Busca un DAC alcanzable desde 'n'; 'path' recibe los nodos (n primero)
bool find_dac(int n, int depth, int* path, int& plen, int prefer) {
    if (depth > 6 || n <= 0 || n >= MAXN) return false;
    path[plen++] = n;
    if (wtype[n] == 0 && (prefer < 0 || n == prefer)) return true;
    if (wtype[n] == 0) { plen--; return false; }
    for (int i = 0; i < nconns[n]; i++) {
        int c = conns[n][i];
        if (c > 0 && c < MAXN && (wtype[c] == 0 || wtype[c] == 2 || wtype[c] == 3)) {
            if (find_dac(c, depth + 1, path, plen, prefer)) return true;
        }
    }
    plen--;
    return false;
}

// Configuración por defecto de un pin (la que dejó la BIOS), con las correcciones conocidas:
// en los HP Pavilion dv5 (IDT 92HD71B) la BIOS marca el altavoz interno (nodo 0x0d) como
// "sin conexión"; Linux lo corrige igual (stac92hd71bxx_fixup_hp_dv5).
uint32_t pin_config(int n) {
    uint32_t cfg = verb(n, 0xF1C00);
    if ((codec_vid >> 16) == 0x111d && (codec_ssid >> 16) == 0x103c && n == 0x0d && (cfg >> 30) == 1) cfg = 0x90170010;
    return cfg;
}

void unmute(int n, bool output, int index) {
    uint32_t caps = (wcaps[n] & (1u << 3)) ? param(n, output ? 0x12 : 0x0D) : afg_out_amp;
    uint32_t gain = caps & 0x7F;               // 0 dB
    if (!gain) gain = (caps >> 8) & 0x7F;      // sin desplazamiento: máximo
    uint32_t payload = (output ? 0x8000u : 0x4000u) | 0x3000u | ((uint32_t)index << 8) | gain;
    verb(n, 0x30000 | payload);
}

bool setup_path(int* path, int plen) {
    // path[0] = pin ... path[plen-1] = DAC
    for (int i = 0; i < plen; i++) {
        int n = path[i];
        verb(n, 0x70500);   // D0
        if (i + 1 < plen) {
            int next = path[i + 1];
            int idx = 0;
            for (int k = 0; k < nconns[n]; k++) if (conns[n][k] == next) idx = k;
            if (wtype[n] != 2 && nconns[n] > 1) verb(n, 0x70100 | (uint32_t)idx);   // selección de conexión
            if (wcaps[n] & (1u << 1)) unmute(n, false, idx);                         // amplificador de entrada
        }
        if (wcaps[n] & (1u << 2)) unmute(n, true, 0);                                // amplificador de salida
    }
    return true;
}

bool configure_codec() {
    uint32_t sub = param(0, 0x04);
    int start = (sub >> 16) & 0xFF, count = sub & 0xFF;
    for (int f = start; f < start + count; f++)
        if ((param(f, 0x05) & 0xFF) == 1) { afg = f; break; }
    if (afg < 0) return false;
    verb(afg, 0x70500);           // grupo de audio en D0
    afg_out_amp = param(afg, 0x12);
    uint32_t vid = param(0, 0x00);
    codec_vid = vid;
    codec_ssid = verb(afg, 0xF2000);
    ksnprintf(dev_name, sizeof dev_name, "HD Audio (códec %04x:%04x)", vid >> 16, vid & 0xFFFF);
    klog("HDA: códec %08x, subsistema %08x\n", codec_vid, codec_ssid);
    // IDT/SigmaTel: el amplificador externo de los altavoces se enciende con el GPIO0
    // (como hace Linux con los 92HD71B y similares)
    gpio_count = (int)(param(afg, 0x11) & 0xFF);
    uint16_t vendor = (uint16_t)(vid >> 16);
    if (gpio_count > 0 && (vendor == 0x111d || vendor == 0x8384)) {
        uint32_t mask = verb(afg, 0xF1600) & 0xFF, dir = verb(afg, 0xF1700) & 0xFF, data = verb(afg, 0xF1500) & 0xFF;
        verb(afg, 0x71600 | (mask | 1));
        verb(afg, 0x71700 | (dir | 1));
        verb(afg, 0x71500 | (data | 1));
        gpio_amp = true;
        klog("HDA: GPIO0 activado (amplificador de los altavoces)\n");
    }
    sub = param(afg, 0x04);
    start = (sub >> 16) & 0xFF;
    count = sub & 0xFF;
    memset(wtype, 0xFF, sizeof wtype);
    for (int n = start; n < start + count && n < MAXN; n++) {
        wcaps[n] = param(n, 0x09);
        wtype[n] = (uint8_t)((wcaps[n] >> 20) & 0xF);
        if (wcaps[n] & (1u << 8)) read_conns(n);
        else nconns[n] = 0;
    }
    // Pines de salida conectados: altavoces, salida de línea y auriculares
    int dac = -1;
    int configured = 0;
    for (int want = 0; want < 3; want++) {
        for (int n = start; n < start + count && n < MAXN; n++) {
            if (wtype[n] != 4) continue;
            uint32_t pincaps = param(n, 0x0C);
            if (!(pincaps & (1u << 4))) continue;          // no tiene salida
            uint32_t cfg = pin_config(n);
            uint32_t conn = cfg >> 30, device = (cfg >> 20) & 0xF;
            if (conn == 1) continue;                       // sin conexión física
            int order = device == 1 ? 0 : (device == 0 ? 1 : (device == 2 ? 2 : -1));
            if (order != want) continue;
            int path[8], plen = 0;
            if (!find_dac(n, 0, path, plen, dac) && !(plen = 0, find_dac(n, 0, path, plen, -1))) continue;
            setup_path(path, plen);
            int this_dac = path[plen - 1];
            if (dac < 0) dac = this_dac;
            verb(this_dac, 0x20000 | 0x0011);              // 48 kHz, 16 bits, estéreo
            verb(this_dac, 0x70600 | (1 << 4));            // flujo 1, canal 0
            uint32_t ctl = (pincaps & (1u << 3)) && device == 2 ? 0xC0 : 0x40;   // salida (+ amplificador de auriculares)
            verb(n, 0x70700 | ctl);
            if (pincaps & (1u << 16)) verb(n, 0x70C00 | 0x02);   // EAPD: activa el amplificador externo
            configured++;
            if (n_out_pins < 8) out_pins[n_out_pins++] = n;
            klog("HDA: salida en el pin %d (%s) por el DAC %d\n", n,
                 device == 1 ? "altavoces" : (device == 2 ? "auriculares" : "línea"), this_dac);
        }
    }
    klog("HDA: %d salidas configuradas (DAC %d)\n", configured, dac);
    return configured > 0;
}
}

bool audio::hda::probe() {
    const PciDevice* dev = nullptr;
    for (auto& d : pci::devices())
        if (d.cls == 0x04 && d.subclass == 0x03) { dev = &d; break; }
    if (!dev) return false;
    pci::enable_bus_master(*dev);
    if (dev->vendor == 0x8086) {
        // TCSEL = 0 (recomendación de Intel para evitar chasquidos)
        uint32_t v = pci::read32(*dev, 0x44);
        pci::write32(*dev, 0x44, v & ~7u);
    }
    uint64_t bar = pci::bar_address(*dev, 0);
    if (!bar) return false;
    regs = (volatile uint8_t*)mm::map_mmio(bar, 0x4000);

    // Reinicio de la controladora
    w32(0x08, r32(0x08) & ~1u);
    wait_for(100, [] { return !(r32(0x08) & 1); });
    pit::sleep_ms(1);
    w32(0x08, r32(0x08) | 1u);
    if (!wait_for(100, [] { return (r32(0x08) & 1) != 0; })) { klog("HDA: la controladora no sale del reinicio\n"); return false; }
    pit::sleep_ms(10);
    uint16_t codecs = r16(0x0E);
    if (!codecs) { klog("HDA: no hay códecs\n"); return false; }

    // CORB y RIRB
    w8(0x4C, 0);
    w8(0x5C, 0);
    wait_for(10, [] { return !(r8(0x4C) & 2) && !(r8(0x5C) & 2); });
    uint8_t cs = r8(0x4E);
    corb_entries = (cs & 0x40) ? 256 : ((cs & 0x20) ? 16 : 2);
    w8(0x4E, (uint8_t)((cs & 0xF0) | (corb_entries == 256 ? 2 : (corb_entries == 16 ? 1 : 0))));
    uint8_t rs = r8(0x5E);
    w8(0x5E, (uint8_t)((rs & 0xF0) | (corb_entries == 256 ? 2 : (corb_entries == 16 ? 1 : 0))));
    uint64_t cp = mm::alloc_page();
    corb = (uint32_t*)mm::phys_to_virt(cp);
    rirb = (uint32_t*)((uint8_t*)corb + 2048);
    w32(0x40, (uint32_t)cp);
    w32(0x44, (uint32_t)(cp >> 32));
    w32(0x50, (uint32_t)(cp + 2048));
    w32(0x54, (uint32_t)((cp + 2048) >> 32));
    w16(0x4A, 0x8000);
    wait_for(10, [] { return (r16(0x4A) & 0x8000) != 0; });
    w16(0x4A, 0);
    wait_for(10, [] { return !(r16(0x4A) & 0x8000); });
    w16(0x48, 0);
    w16(0x58, 0x8000);
    w16(0x5A, 1);
    rirb_rp = 0;
    w8(0x4C, 2);
    w8(0x5C, 2);

    for (int c = 0; c < 15; c++) {
        if (!(codecs & (1 << c))) continue;
        codec = c;
        if (configure_codec()) break;
        codec = -1;
    }
    if (codec < 0) { klog("HDA: ningún códec tiene salidas utilizables\n"); return false; }
    uint16_t gcap = r16(0x00);
    int iss = (gcap >> 8) & 0xF;
    sd = 0x80 + (uint32_t)iss * 0x20;
    klog("Sonido: %s en %02x:%02x.%u\n", dev_name, dev->bus, dev->dev, dev->func);
    return true;
}

void audio::hda::stop() {
    if (!regs || !sd) return;
    w32(sd, r32(sd) & ~2u);
    wait_for(10, [] { return !(r32(sd) & 2); });
}

// Búfer circular: cuatro descriptores que la controladora recorre sin parar
bool audio::hda::start_ring(uint64_t bdl_phys, uint64_t ring_phys, uint32_t bytes) {
    if (!regs || codec < 0) return false;
    stop();
    auto* bdl = (uint8_t*)mm::phys_to_virt(bdl_phys);
    memset(bdl, 0, 4096);
    uint32_t part = bytes / 4;
    for (int i = 0; i < 4; i++) {
        uint8_t* e = bdl + i * 16;
        *(uint64_t*)e = ring_phys + (uint64_t)i * part;
        *(uint32_t*)(e + 8) = part;
        *(uint32_t*)(e + 12) = 0;
    }
    // reinicio del flujo
    w32(sd, r32(sd) | 1u);
    wait_for(10, [] { return (r32(sd) & 1) != 0; });
    w32(sd, r32(sd) & ~1u);
    wait_for(10, [] { return !(r32(sd) & 1); });
    w32(sd + 0x18, (uint32_t)bdl_phys);
    w32(sd + 0x1C, (uint32_t)(bdl_phys >> 32));
    w32(sd + 0x08, bytes);
    w16(sd + 0x0C, 3);
    w16(sd + 0x12, 0x0011);                           // 48 kHz, 16 bits, estéreo
    w32(sd, (r32(sd) & 0xFF0FFFFFu) | (1u << 20));   // número de flujo 1
    asm volatile("mfence" ::: "memory");
    w32(sd, r32(sd) | 2u);                            // en marcha
    return true;
}

uint32_t audio::hda::ring_pos() { return regs && sd ? r32(sd + 0x04) : 0; }   // LPIB

const char* audio::hda::name() { return dev_name; }

// Informe del códec para diagnosticar problemas de sonido (orden SONIDO)
void audio::hda::describe(String& out) {
    if (!regs) { out += "HDA: no hay controladora\r\n"; return; }
    out += String::format("Controladora: GCAP=%04x STATESTS=%04x órdenes %s\r\n", r16(0x00), r16(0x0E),
                          immediate ? "inmediatas" : "CORB/RIRB");
    if (codec < 0) { out += "Ningún códec utilizable\r\n"; return; }
    out += String::format("Códec %d: %08x, subsistema %08x, AFG %d, GPIO: %d%s\r\n", codec, codec_vid, codec_ssid, afg,
                          gpio_count, gpio_amp ? " (GPIO0 activado)" : "");
    if (afg >= 0 && gpio_count)
        out += String::format("  GPIO máscara %02x dirección %02x datos %02x\r\n", verb(afg, 0xF1600) & 0xFF,
                              verb(afg, 0xF1700) & 0xFF, verb(afg, 0xF1500) & 0xFF);
    out += "Pines de salida configurados:";
    for (int i = 0; i < n_out_pins; i++) out += String::format(" %d", out_pins[i]);
    out += "\r\n\r\n";
    static const char* types[] = {"salida", "entrada", "mezclador", "selector", "pin", "potencia", "volumen", "pitido"};
    uint32_t sub = param(afg, 0x04);
    int start = (sub >> 16) & 0xFF, count = sub & 0xFF;
    for (int n = start; n < start + count && n < MAXN; n++) {
        uint32_t caps = wcaps[n];
        int t = wtype[n];
        out += String::format("Nodo %d: %s, caps %08x, energía %x", n, t < 8 ? types[t] : "otro", caps, verb(n, 0xF0500) & 0xFF);
        if (nconns[n]) {
            out += ", conexiones";
            for (int i = 0; i < nconns[n]; i++) out += String::format(" %d", conns[n][i]);
            out += String::format(" (elegida %d)", verb(n, 0xF0100) & 0xFF);
        }
        out += "\r\n";
        if (t == 4) {
            uint32_t raw = verb(n, 0xF1C00);
            out += String::format("    pin: caps %08x, configuración %08x%s, control %02x, EAPD %02x\r\n", param(n, 0x0C), raw,
                                  pin_config(n) != raw ? " (corregida)" : "", verb(n, 0xF0700) & 0xFF, verb(n, 0xF0C00) & 0xFF);
        }
        if (caps & (1u << 2))
            out += String::format("    amp. salida: caps %08x, ganancia I %02x D %02x\r\n",
                                  (caps & (1u << 3)) ? param(n, 0x12) : afg_out_amp, verb(n, 0xBA000) & 0xFF, verb(n, 0xB8000) & 0xFF);
        if (caps & (1u << 1))
            out += String::format("    amp. entrada 0: ganancia I %02x D %02x\r\n", verb(n, 0xB2000) & 0xFF, verb(n, 0xB0000) & 0xFF);
    }
    if (sd)
        out += String::format("\r\nFlujo: CTL %08x STS %02x LPIB %u CBL %u\r\n", r32(sd), r8(sd + 3), r32(sd + 0x04), r32(sd + 0x08));
}

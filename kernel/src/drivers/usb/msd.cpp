// Almacenamiento masivo USB (pendrives y discos externos): protocolo Bulk-Only (BOT)
// con órdenes SCSI. Cada dispositivo se publica como un disco extraíble más.
#include "drivers/usb/usb_internal.h"
#include "drivers/block.h"
#include "fs/vfs.h"
#include "lib/log.h"
#include "lib/string.h"
#include "lib/i18n.h"
#include "lib/notice.h"
#include "arch/idt.h"

namespace {
constexpr uint32_t CBW_SIG = 0x43425355, CSW_SIG = 0x53425355;
int disk_counter = 0;

struct [[gnu::packed]] Cbw {
    uint32_t sig, tag, length;
    uint8_t flags, lun, cb_len;
    uint8_t cb[16];
};
struct [[gnu::packed]] Csw {
    uint32_t sig, tag, residue;
    uint8_t status;
};

class UsbDisk : public BlockDevice {
public:
    usb::Device* dev;   // nullptr cuando se ha desconectado
    uint8_t iface, ep_in, ep_out;
    uint16_t mps_in, mps_out;
    uint8_t tog_in = 0, tog_out = 0;
    uint32_t tag = 1;

    bool clear_halt(uint8_t ep_addr) {
        usb::SetupPacket s{0x02, 1, 0, ep_addr, 0};   // CLEAR_FEATURE(ENDPOINT_HALT)
        bool ok = dev->hc->control(*dev, s, nullptr);
        if (ep_addr & 0x80) tog_in = 0; else tog_out = 0;
        return ok;
    }

    // Recuperación tras un error de protocolo (BOT 5.3.4)
    void reset_recovery() {
        usb::SetupPacket s{0x21, 0xFF, 0, iface, 0};   // Bulk-Only Mass Storage Reset
        dev->hc->control(*dev, s, nullptr);
        clear_halt(0x80 | ep_in);
        clear_halt(ep_out);
    }

    // Una orden SCSI completa: CBW, datos (opcional) y CSW. Devuelve el estado del CSW
    // (0 correcto, 1 fallo de la orden) o -1 si falló el transporte.
    int command(const uint8_t* cb, int cb_len, void* data, uint32_t len, bool in) {
        if (!dev) return -1;
        Cbw w{};
        w.sig = CBW_SIG;
        w.tag = tag++;
        w.length = len;
        w.flags = in ? 0x80 : 0;
        w.cb_len = (uint8_t)cb_len;
        memcpy(w.cb, cb, (size_t)cb_len);
        if (dev->hc->bulk(*dev, ep_out, false, &w, 31, tog_out, mps_out) != 31) { reset_recovery(); return -1; }
        if (len) {
            int r = in ? dev->hc->bulk(*dev, ep_in, true, data, len, tog_in, mps_in)
                       : dev->hc->bulk(*dev, ep_out, false, data, len, tog_out, mps_out);
            if (r == -2) clear_halt(in ? (0x80 | ep_in) : ep_out);   // el dispositivo cortó la fase de datos
            else if (r < 0) { reset_recovery(); return -1; }
        }
        Csw s{};
        int r = dev->hc->bulk(*dev, ep_in, true, &s, 13, tog_in, mps_in);
        if (r == -2) {   // CSW detenido: se borra y se reintenta una vez
            clear_halt(0x80 | ep_in);
            r = dev->hc->bulk(*dev, ep_in, true, &s, 13, tog_in, mps_in);
        }
        if (r != 13 || s.sig != CSW_SIG || s.tag != w.tag || s.status > 1) { reset_recovery(); return -1; }
        return s.status;
    }

    void request_sense() {
        uint8_t cb[6] = {0x03, 0, 0, 0, 18, 0};
        uint8_t sense[18] = {};
        command(cb, 6, sense, 18, true);
    }

    bool rw(bool write, uint64_t lba, uint32_t count, void* buf) {
        if (!dev || lba + count > sectors) return false;
        auto* p = (uint8_t*)buf;
        while (count) {
            uint32_t n = min(count, dev->hc->max_bulk() / 512);
            uint8_t cb[10] = {(uint8_t)(write ? 0x2A : 0x28), 0,
                              (uint8_t)(lba >> 24), (uint8_t)(lba >> 16), (uint8_t)(lba >> 8), (uint8_t)lba,
                              0, (uint8_t)(n >> 8), (uint8_t)n, 0};
            bool ok = false;
            for (int attempt = 0; attempt < 3 && !ok && dev; attempt++) {
                int st = command(cb, 10, p, n * 512, !write);
                if (st == 0) ok = true;
                else if (st == 1) request_sense();
            }
            if (!ok) { klog("%s: error al %s el sector %lu\n", name, write ? "escribir" : "leer", lba); return false; }
            p += n * 512;
            lba += n;
            count -= n;
        }
        return true;
    }

    bool read(uint64_t lba, uint32_t count, void* buf) override { return rw(false, lba, count, buf); }
    bool write(uint64_t lba, uint32_t count, const void* buf) override { return rw(true, lba, count, (void*)buf); }
    bool flush() override {
        if (!dev) return false;
        uint8_t cb[10] = {0x35};   // SYNCHRONIZE CACHE(10)
        command(cb, 10, nullptr, 0, false);
        return true;
    }
};

void trim_copy(char* dst, const uint8_t* src, int n) {
    int len = n;
    while (len > 0 && (src[len - 1] == ' ' || !src[len - 1])) len--;
    for (int i = 0; i < len; i++) dst[i] = (src[i] >= 32 && src[i] < 127) ? (char)src[i] : '?';
    dst[len] = 0;
}
}

bool usb::msd_attach(Device& d, uint8_t iface, uint8_t ep_in, uint16_t mps_in, uint8_t ep_out, uint16_t mps_out) {
    auto* u = new UsbDisk();
    u->dev = &d;
    u->iface = iface;
    u->ep_in = ep_in;
    u->ep_out = ep_out;
    u->mps_in = mps_in;
    u->mps_out = mps_out;
    u->removable = true;

    // Si no se puede usar, se anota en el registro y se avisa al usuario
    auto fail = [&](const String& why) {
        klog("USB %04x:%04x: %s\n", d.vendor, d.product, why.c_str());
        String who = u->model[0] ? String(u->model) : String::format("USB %04x:%04x", d.vendor, d.product);
        notice::post(T("Disco extraíble"),
                     String::format(T("Se ha conectado \"%s\", pero no se puede usar:\n\n%s"), who.c_str(), why.c_str()).c_str());
        delete u;
        return false;
    };

    // INQUIRY: algunos pendrives no contestan a la primera tras la configuración
    uint8_t inq[36] = {};
    uint8_t cb_inq[6] = {0x12, 0, 0, 0, 36, 0};
    bool answered = false;
    for (int i = 0; i < 3 && !answered; i++) {
        answered = u->command(cb_inq, 6, inq, 36, true) == 0;
        if (!answered) usb::sleep(100);
    }
    if (!answered) return fail(T("El dispositivo no responde a las órdenes de almacenamiento."));
    char vendor[12], product[20];
    trim_copy(vendor, inq + 8, 8);
    trim_copy(product, inq + 16, 16);
    ksnprintf(u->model, sizeof u->model, "%s %s", vendor, product);
    if ((inq[0] & 0x1F) != 0)   // no es un dispositivo de bloques (p. ej. un lector de CD)
        return fail(String::format(T("No es un disco (dispositivo SCSI de tipo %u)."), inq[0] & 0x1F));
    // Hasta que el medio esté listo (los lectores de tarjetas y algunos pendrives tardan),
    // como mucho 5 segundos; si ni siquiera hay comunicación se deja de insistir antes.
    uint8_t tur[6] = {0x00};
    uint64_t until = pit::ticks() + 5000;
    int transport_errors = 0;
    while (pit::ticks() < until && transport_errors < 3) {
        int st = u->command(tur, 6, nullptr, 0, false);
        if (st == 0) break;
        if (st < 0) transport_errors++;
        else u->request_sense();
        usb::sleep(100);
    }
    uint8_t cap[8] = {};
    uint8_t cb_cap[10] = {0x25};   // READ CAPACITY(10)
    bool got = false;
    for (int i = 0; i < 3 && !got; i++) {
        got = u->command(cb_cap, 10, cap, 8, true) == 0;
        if (!got) u->request_sense();
    }
    if (!got) return fail(T("No hay ningún medio insertado o no se pudo leer su capacidad."));
    uint32_t last = ((uint32_t)cap[0] << 24) | ((uint32_t)cap[1] << 16) | ((uint32_t)cap[2] << 8) | cap[3];
    uint32_t bs = ((uint32_t)cap[4] << 24) | ((uint32_t)cap[5] << 16) | ((uint32_t)cap[6] << 8) | cap[7];
    if (bs != 512)
        return fail(String::format(T("Usa sectores de %u bytes; BrysOS sólo admite sectores de 512 bytes."), bs));
    u->sectors = (uint64_t)last + 1;
    ksnprintf(u->name, sizeof u->name, "USB%d", disk_counter++);
    d.storage = u;
    ksnprintf(d.desc, sizeof d.desc, "Almacenamiento USB: %s", u->model);
    klog("%s: %s, %lu MiB (extraíble)\n", u->name, u->model, u->sectors >> 11);
    block::add_disk(u);
    vfs::disk_added(u);
    return true;
}

void usb::msd_detach(Device& d) {
    auto* u = (UsbDisk*)d.storage;
    if (!u) return;
    klog("%s: dispositivo retirado\n", u->name);
    vfs::disk_removed(u);
    block::remove_disk(u);
    // El objeto no se libera: alguna ventana puede conservar aún el puntero al disco,
    // y sin dispositivo todas sus operaciones fallan sin tocar el hardware.
    u->dev = nullptr;
    d.storage = nullptr;
}

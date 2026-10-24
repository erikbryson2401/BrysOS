// Diagnóstico de red (orden RED). Por ahora: tarjetas de red PCI, el bus interno de las
// tarjetas Broadcom (driver ssb de Linux) y si está el firmware de la WiFi.
#include "net/diag.h"
#include "drivers/pci.h"
#include "fs/vfs.h"
#include "lib/log.h"
#include "lib/string.h"
#include "linux/brys/linux.h"
#include "mm/memory.h"
#include "proc/sched.h"

namespace {
// Archivos de firmware de b43 que necesita la BCM4322 (núcleo 802.11 rev. 16, N-PHY)
const char* const FW_DIR = "C:\\BrysOS\\Firmware\\b43";
const char* const FW_FILES[] = {"ucode16_mimo.fw", "n0initvals16.fw", "n0bsinitvals16.fw"};

void add_linux_log(String& out, unsigned from) {
    const char* log = log_buffer();
    unsigned len = log_length();
    if (from > len) from = 0;
    String lines;
    const char* p = log + from;
    while (*p) {
        const char* e = p;
        while (*e && *e != '\n') e++;
        if (!strncmp(p, "linux: ", 7)) {
            lines.append(p, (size_t)(e - p));
            lines += "\r\n";
        }
        p = *e ? e + 1 : e;
    }
    out += "\r\nMensajes de los drivers de Linux:\r\n";
    out += lines.empty() ? String("  (ninguno)\r\n") : lines;
}
}

void net::diagnostics(String& out) {
    out += "Tarjetas de red PCI:\r\n";
    int n = 0;
    for (auto& d : pci::devices()) {
        if (d.cls != 0x02) continue;
        out += String::format("  %02x:%02x.%u  %04x:%04x  %s (rev. %02x, IRQ %u)\r\n", d.bus, d.dev, d.func, d.vendor,
                              d.device, d.subclass == 0x80 ? "Inalámbrica" : (d.subclass == 0 ? "Ethernet" : "Red"),
                              d.rev, d.irq);
        n++;
    }
    if (!n) out += "  (ninguna)\r\n";

    // Los drivers de Linux arrancan la primera vez que se pide el informe
    unsigned mark = log_length();
    if (brys_linux_start()) klog("Red: drivers de Linux iniciados\n");

    out += "\r\nBus SSB (Broadcom):\r\n";
    char* rep = (char*)mm::kmalloc(8192);
    if (rep) {
        brys_ssb_report(rep, 8192);
        out += rep;
        mm::kfree(rep);
    }

    out += String::format("\r\nFirmware de la WiFi (%s):\r\n", FW_DIR);
    for (const char* f : FW_FILES) {
        String path = vfs::join(FW_DIR, f);
        VfsEntry e;
        bool ok = vfs::stat(path.c_str(), e) && !e.dir;
        out += String::format("  %-20s %s\r\n", f, ok ? String::format("%u bytes", e.size).c_str() : "no está");
    }
    add_linux_log(out, mark);
}

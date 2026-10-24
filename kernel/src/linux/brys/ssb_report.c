/* Informe de los buses SSB (tarjetas Broadcom) para la orden RED: chip, núcleos internos y
 * datos de la SPROM. Lee las estructuras que ha rellenado el driver ssb de Linux. */
#include "../drivers/ssb/ssb_private.h"   /* primero: define pr_fmt */
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/ssb/ssb.h>
#include <brys/linux.h>

struct rep {
    char* buf;
    size_t size, len;
};

static void add(struct rep* r, const char* fmt, ...) __printf(2, 3);
static void add(struct rep* r, const char* fmt, ...) {
    if (r->len >= r->size) return;
    va_list ap;
    va_start(ap, fmt);
    int n = vscnprintf(r->buf + r->len, r->size - r->len, fmt, ap);
    va_end(ap);
    r->len += (size_t)n;
}

size_t brys_ssb_report(char* buf, size_t size) {
    struct rep r = {buf, size, 0};
    if (size) buf[0] = 0;
    int found = 0;
    for (int i = 0; i < brys_pci_bound_count(); i++) {
        struct pci_dev* pdev = brys_pci_bound(i);
        if (!pdev->driver || strcmp(pdev->driver->name, "b43-pci-bridge")) continue;
        struct ssb_bus* bus = pci_get_drvdata(pdev);
        if (!bus) continue;
        found++;
        add(&r, "Tarjeta %s (%04x:%04x, subsistema %04x:%04x, IRQ %u, %s)\r\n", pci_name(pdev), pdev->vendor,
            pdev->device, pdev->subsystem_vendor, pdev->subsystem_device, pdev->irq,
            pci_is_pcie(pdev) ? "PCI Express" : "PCI");
        add(&r, "  BAR0: %llx (%llu KiB)\r\n", (unsigned long long)pci_resource_start(pdev, 0),
            (unsigned long long)pci_resource_len(pdev, 0) / 1024);
        add(&r, "  Chip: BCM%04X rev. %u, encapsulado %u\r\n", bus->chip_id, bus->chip_rev, bus->chip_package);
        add(&r, "  Placa: fabricante %04X, tipo %04X, rev. %u\r\n", bus->boardinfo.vendor, bus->boardinfo.type,
            bus->sprom.board_rev);
        add(&r, "  SPROM: rev. %u, MAC %pM, país \"%c%c\", antenas %u/%u\r\n", bus->sprom.revision,
            bus->sprom.il0mac, bus->sprom.alpha2[0] ? bus->sprom.alpha2[0] : '-',
            bus->sprom.alpha2[1] ? bus->sprom.alpha2[1] : '-', bus->sprom.ant_available_a,
            bus->sprom.ant_available_bg);
        add(&r, "  ChipCommon: capacidades %08X, PMU rev. %u\r\n", bus->chipco.capabilities, bus->chipco.pmu.rev);
        add(&r, "  Núcleos internos: %u\r\n", bus->nr_devices);
        for (int c = 0; c < bus->nr_devices; c++) {
            struct ssb_device* d = &bus->devices[c];
            add(&r, "    %d: %-22s id %03X rev. %2u fabricante %04X%s\r\n", c, ssb_core_name(d->id.coreid),
                d->id.coreid, d->id.revision, d->id.vendor,
                d->dev && d->dev->driver ? "  (con driver)" : "");
        }
    }
    if (!found) add(&r, "No se ha encontrado ninguna tarjeta Broadcom con bus SSB.\r\n");
    return r.len;
}

/* Lo que espero leer cuando esto corra en el dv5: "Chip: BCM4322", un núcleo IEEE 802.11
 * rev. 16 y una MAC que empiece por algo de HP o Broadcom. Si sale otra cosa, también será
 * una buena historia. — Claude */

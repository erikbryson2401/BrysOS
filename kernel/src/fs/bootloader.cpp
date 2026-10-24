// Instalación de GRUB 2 (BIOS y UEFI) y generación de su menú
#include "sys/paths.h"
#include "fs/bootloader.h"
#include "fs/fat32.h"
#include "fs/vfs.h"
#include "boot/bootinfo.h"
#include "lib/i18n.h"
#include "lib/log.h"
#include "lib/string.h"

namespace {
uint64_t first_partition_start(const DiskLayout& l) {
    uint64_t first = ~0ull;
    for (auto& p : l.parts) if (p.start < first) first = p.start;
    return first;
}

// Sector donde va core.img: después de la tabla GPT si la hay
uint64_t core_lba(const DiskLayout& l) { return l.gpt ? 34 : 1; }

uint32_t core_sectors() {
    const BootModule* m = boot_module("core.img");
    return m ? (uint32_t)((m->size + 511) / 512) : 0;
}

void wr64(uint8_t* p, uint64_t v) { for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8 * i)); }
void wr16(uint8_t* p, uint16_t v) { p[0] = v & 0xFF; p[1] = v >> 8; }
}

bool bootloader::bios_gap_ok(const DiskLayout& l) {
    uint32_t n = core_sectors();
    return n && core_lba(l) + n <= first_partition_start(l);
}

bool bootloader::install_bios(DiskLayout& l, Vec<uint8_t>& backup, const char*& err) {
    const BootModule* boot = boot_module("boot.img");
    const BootModule* core = boot_module("core.img");
    if (!boot || !core || boot->size != 512) { err = T("No se encuentran los archivos de GRUB en el medio de instalación."); return false; }
    if (!bios_gap_ok(l)) {
        err = T("No hay espacio para GRUB antes de la primera partición (hace falta que empiece en el sector 2048, como en Windows Vista o posterior).");
        return false;
    }
    BlockDevice* d = l.disk;
    uint32_t n = core_sectors();
    uint64_t lba = core_lba(l);
    // copia de seguridad: MBR y sectores que se van a sobrescribir
    backup.resize((size_t)(lba + n) * 512);
    if (!d->read(0, (uint32_t)(lba + n), backup.data())) { err = T("No se puede leer el principio del disco."); return false; }

    // core.img: la primera parte (diskboot.img) lleva una lista de bloques con la ubicación del resto
    auto* img = (uint8_t*)mm::kcalloc((size_t)n * 512);
    memcpy(img, core->data(), core->size);
    uint8_t* list = img + 0x200 - 12;
    wr64(list, lba + 1);
    wr16(list + 8, (uint16_t)(n - 1));
    wr16(list + 10, 0x820);
    bool ok = d->write(lba, n, img);
    mm::kfree(img);
    if (!ok) { err = T("Error al escribir GRUB en el disco."); return false; }

    // boot.img en el MBR, conservando la firma de disco y la tabla de particiones
    uint8_t mbr[512];
    memcpy(mbr, boot->data(), 512);
    memcpy(mbr + 0x1B8, backup.data() + 0x1B8, 512 - 0x1B8);
    wr64(mbr + 0x5C, lba);   // sector de core.img
    mbr[0x64] = 0xFF;        // usar la unidad indicada por la BIOS
    mbr[0x66] = 0x90;        // como grub-setup en discos duros: no confiar en DL si la BIOS lo pasa mal
    mbr[0x67] = 0x90;
    if (!d->write(0, 1, mbr) || !d->flush()) { err = T("Error al escribir el MBR."); return false; }
    klog("GRUB instalado en %s (core.img en el sector %lu, %u sectores)\n", d->name, lba, n);
    return true;
}

bool bootloader::install_efi(DiskLayout& l, const PartInfo& esp, String& note, const char*& err) {
    const BootModule* efi = boot_module("grubx64.efi");
    if (!efi) { err = T("No se encuentra grubx64.efi en el medio de instalación."); return false; }
    if (esp.fs != FsType::FAT32) { err = T("La partición del sistema EFI no es FAT32."); return false; }
    Partition* p = part::open(l.disk, esp);
    char letter = vfs::mount_rw(p);
    if (!letter) { delete p; err = T("No se puede montar la partición del sistema EFI."); return false; }
    String base = String::format("%c:\\", letter);
    vfs::mkdir((base + "EFI").c_str());
    vfs::mkdir((base + "EFI\\BrysOS").c_str());
    bool ok = vfs::write_file((base + "EFI\\BrysOS\\grubx64.efi").c_str(), efi->data(), (uint32_t)efi->size);
    // Ruta de arranque por omisión: sólo si está libre, para no pisar otro sistema
    if (ok) {
        vfs::mkdir((base + "EFI\\BOOT").c_str());
        String fallback = base + "EFI\\BOOT\\BOOTX64.EFI";
        if (!vfs::exists(fallback.c_str())) {
            vfs::write_file(fallback.c_str(), efi->data(), (uint32_t)efi->size);
            note = T("GRUB se ha instalado como cargador predeterminado del firmware UEFI.");
        } else {
            note = T("Ya existía otro cargador predeterminado. Elija \"EFI\\BrysOS\\grubx64.efi\" en el menú de arranque del firmware (o añádalo en su configuración).");
        }
    }
    vfs::unmount(letter);
    if (!ok) err = T("No se pudo copiar GRUB a la partición del sistema EFI.");
    return ok;
}

Vec<OtherOs> bootloader::detect_systems(Vec<DiskLayout>& disks, bool uefi) {
    Vec<OtherOs> out;
    bool windows_bios = false;
    for (auto& l : disks) {
        for (auto& p : l.parts) {
            if (p.fs == FsType::NTFS && !uefi) windows_bios = true;
            if (uefi && p.esp && p.fs == FsType::FAT32) {
                Partition part(l.disk, p.start, p.count, p.number);
                if (Fat32* fs = Fat32::mount(&part, true)) {
                    FatEntry e;
                    if (fs->resolve("\\EFI\\Microsoft\\Boot\\bootmgfw.efi", e)) {
                        OtherOs o;
                        o.name = "Windows";
                        o.script = String::format(
                            "\tsearch --no-floppy --fs-uuid --set=root %s\n"
                            "\tchainloader /EFI/Microsoft/Boot/bootmgfw.efi\n", p.uuid);
                        out.push(move_val(o));
                    }
                    delete fs;
                }
            }
            if (p.fs == FsType::EXT && p.uuid[0]) {
                OtherOs o;
                o.name = String::format("Linux (%s)", p.label[0] ? p.label : p.uuid);
                o.script = String::format(
                    "\tsearch --no-floppy --fs-uuid --set=root %s\n"
                    "\tif [ -f /boot/grub/grub.cfg ]; then\n"
                    "\t\tconfigfile /boot/grub/grub.cfg\n"
                    "\telif [ -f /grub/grub.cfg ]; then\n"
                    "\t\tconfigfile /grub/grub.cfg\n"
                    "\telse\n"
                    "\t\techo \"%s\"\n"
                    "\t\tsleep 5\n"
                    "\tfi\n", p.uuid, T("No se encontró el menú de arranque de este sistema."));
                out.push(move_val(o));
            }
        }
    }
    if (windows_bios) {
        // Vista y posteriores usan bootmgr; XP usa ntldr
        OtherOs o;
        o.name = "Windows";
        o.script = "\tif search --no-floppy --file --set=root /bootmgr; then\n"
                   "\t\tntldr /bootmgr\n"
                   "\telif search --no-floppy --file --set=root /ntldr; then\n"
                   "\t\tntldr /ntldr\n"
                   "\tfi\n";
        out.push(move_val(o));
    }
    return out;
}

String bootloader::bsl_module_lines(const Vec<String>& bsl_files) {
    String c;
    for (auto& f : bsl_files) c += String::format("\tmodule2 %s%s %s\n", sys::GRUB_BSL_DIR, f.c_str(), f.c_str());
    return c;
}

String bootloader::grub_cfg(const char* uuid, const char* args, const Vec<OtherOs>& others, int timeout,
                            const Vec<String>& bsl_files) {
    String c;
    c += "# Menú de arranque generado por el instalador de BrysOS\n";
    c += String::format("set timeout=%d\nset default=0\n", timeout);
    c += "set menu_color_normal=white/blue\nset menu_color_highlight=black/light-gray\n\n";
    c += "menuentry \"BrysOS 98\" {\n";
    c += String::format("\tsearch --no-floppy --fs-uuid --set=root %s\n", uuid);
    c += String::format("\tmultiboot2 %s root=%s %s\n", sys::GRUB_KERNEL, uuid, args);
    // bibliotecas del sistema: GRUB las deja en memoria junto con el núcleo
    c += bsl_module_lines(bsl_files);
    // modo gráfico preferido: el nativo del portátil (1280x800) y, si no, 1024x768.
    // Debe ir después de multiboot2, que fija gfxpayload a partir de la cabecera del núcleo.
    c += "\tset gfxpayload=1280x800x32,1366x768x32,1024x768x32,auto\n";
    c += "\tboot\n}\n\n";
    for (auto& o : others) {
        c += String::format("menuentry \"%s\" {\n", o.name.c_str());
        c += o.script;
        c += "}\n\n";
    }
    c += String::format("menuentry \"%s\" {\n\treboot\n}\n", T("Reiniciar"));
    c += String::format("menuentry \"%s\" {\n\thalt\n}\n", T("Apagar"));
    return c;
}

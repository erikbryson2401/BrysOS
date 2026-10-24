#pragma once
#include "fs/partition.h"

// Otro sistema operativo detectado en los discos, con su entrada de menú de GRUB
struct OtherOs {
    String name;
    String script;   // órdenes de GRUB dentro de la entrada
};

namespace bootloader {
Vec<OtherOs> detect_systems(Vec<DiskLayout>& disks, bool uefi);
String grub_cfg(const char* root_uuid, const char* kernel_args, const Vec<OtherOs>& others, int timeout,
                const Vec<String>& bsl_files);
// Líneas module2 que cargan las bibliotecas .bsl (dentro de la entrada de BrysOS)
String bsl_module_lines(const Vec<String>& bsl_files);
// BIOS: boot.img en el MBR y core.img en el hueco anterior a la primera partición.
// 'backup' recibe los sectores originales (para poder restaurarlos).
bool install_bios(DiskLayout& l, Vec<uint8_t>& backup, const char*& err);
bool bios_gap_ok(const DiskLayout& l);
// UEFI: copia grubx64.efi a la partición del sistema EFI
bool install_efi(DiskLayout& l, const PartInfo& esp, String& note, const char*& err);
}

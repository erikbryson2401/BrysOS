#pragma once
// Rutas del sistema. Todo lo que pertenece a BrysOS vive dentro de \BrysOS en la partición
// del sistema (C:) y en ningún otro sitio. La carpeta está protegida (vfs::SystemWrite) y es
// la única que pueden modificar los paquetes de actualización .bsi.
namespace sys {
constexpr const char* DIR = "C:\\BrysOS";
constexpr const char* KERNEL = "C:\\BrysOS\\kernel.elf";
constexpr const char* KERNEL_OLD = "C:\\BrysOS\\kernel.old";     // versión anterior (menú de GRUB)
constexpr const char* CONFIG = "C:\\BrysOS\\config.ini";
constexpr const char* GRUB_CFG = "C:\\BrysOS\\grub.cfg";
constexpr const char* VERSION = "C:\\BrysOS\\version.txt";       // versión instalada en el disco
constexpr const char* MBR_BACKUP = "C:\\BrysOS\\mbr-original.bin";
constexpr const char* UPDATE_JOURNAL = "C:\\BrysOS\\update.pending";
constexpr const char* UPDATE_LOG = "C:\\BrysOS\\actualizaciones.log";
constexpr const char* WALLPAPER = "C:\\BrysOS\\fondo.bmp";          // fondo de escritorio

// Las mismas, relativas a la raíz de la partición (el instalador la monta con otra letra)
constexpr const char* REL_DIR = "BrysOS";
constexpr const char* REL_KERNEL = "BrysOS\\kernel.elf";
constexpr const char* REL_CONFIG = "BrysOS\\config.ini";
constexpr const char* REL_GRUB_CFG = "BrysOS\\grub.cfg";
constexpr const char* REL_VERSION = "BrysOS\\version.txt";
constexpr const char* REL_MBR_BACKUP = "BrysOS\\mbr-original.bin";

// Rutas que ve GRUB (FAT no distingue mayúsculas)
constexpr const char* GRUB_KERNEL = "/brysos/kernel.elf";
constexpr const char* GRUB_KERNEL_OLD = "/brysos/kernel.old";
constexpr const char* GRUB_BSL_DIR = "/brysos/bsl/";

// Bibliotecas del sistema (.bsl)
constexpr const char* BSL_DIR = "C:\\BrysOS\\BSL";
constexpr const char* REL_BSL_DIR = "BrysOS\\BSL";
}

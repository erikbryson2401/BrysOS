#pragma once
#include "drivers/block.h"
#include "lib/vec.h"

enum class FsType { UNKNOWN, EMPTY, FAT32, FAT16, EXFAT, NTFS, EXT, SWAP, EXTENDED };

struct PartInfo {
    int number = 0;          // número visible: MBR 1-4 primarias, 5+ lógicas; GPT índice+1
    int slot = -1;           // MBR: ranura 0-3 (-1 lógica); GPT: índice de entrada
    bool logical = false;
    uint64_t start = 0, count = 0;
    uint8_t mbr_type = 0;
    uint8_t gpt_type[16] = {};
    bool esp = false;        // partición del sistema EFI
    bool active = false;
    FsType fs = FsType::UNKNOWN;
    char label[40] = {};
    char uuid[48] = {};      // identificador en el formato que usa GRUB
    uint32_t fat_serial = 0;
    bool has_brysos = false; // contiene \brysos\kernel.elf
};

struct FreeSpace {
    uint64_t start = 0, count = 0;
};

struct DiskLayout {
    BlockDevice* disk = nullptr;
    bool gpt = false;
    bool blank = false;      // sin tabla de particiones
    uint64_t first_usable = 2048, last_usable = 0;
    int free_slots = 0;      // ranuras MBR primarias libres
    Vec<PartInfo> parts;
    Vec<FreeSpace> free;
};

namespace part {
bool read_layout(BlockDevice* disk, DiskLayout& out);
// Crea una partición FAT32 primaria (MBR) o de datos básicos (GPT) en el espacio libre
bool create(DiskLayout& l, uint64_t start, uint64_t count, PartInfo& out, const char*& err);
// Cambia el tipo de una partición existente a FAT32 (antes de formatearla)
bool set_fat32_type(DiskLayout& l, const PartInfo& p, const char*& err);
// Borra todo el disco y crea una única partición FAT32
bool whole_disk(BlockDevice* disk, PartInfo& out, const char*& err);
Partition* open(BlockDevice* disk, const PartInfo& p);
const char* fs_name(FsType t);
uint32_t crc32(const void* data, size_t len, uint32_t crc = 0);
}

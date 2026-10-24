#pragma once
#include <stdint.h>
#include "lib/vec.h"

// Dispositivo de bloques de 512 bytes por sector
class BlockDevice {
public:
    char name[32] = {};      // "AHCI0", "ATA1", "AHCI0p1"...
    char model[48] = {};
    uint64_t sectors = 0;
    bool removable = false;
    BlockDevice* parent = nullptr; // disco al que pertenece una partición
    uint64_t offset = 0;           // LBA inicial dentro del disco padre

    virtual ~BlockDevice() {}
    virtual bool read(uint64_t lba, uint32_t count, void* buf) = 0;
    virtual bool write(uint64_t lba, uint32_t count, const void* buf) = 0;
    virtual bool flush() { return true; }
};

// Vista de una partición sobre un disco
class Partition : public BlockDevice {
public:
    uint8_t type = 0;    // tipo MBR (0x0B/0x0C = FAT32) o 0xEE para GPT
    Partition(BlockDevice* disk, uint64_t start, uint64_t count, int index);
    bool read(uint64_t lba, uint32_t count, void* buf) override;
    bool write(uint64_t lba, uint32_t count, const void* buf) override;
    bool flush() override { return parent->flush(); }
};

// Disco en memoria (para cuando no hay disco BRYSOS)
class RamDisk : public BlockDevice {
public:
    uint8_t* data;
    explicit RamDisk(uint64_t sectors);
    bool read(uint64_t lba, uint32_t count, void* buf) override;
    bool write(uint64_t lba, uint32_t count, const void* buf) override;
};

namespace block {
void add_disk(BlockDevice* d);
void remove_disk(BlockDevice* d);   // disco extraíble desconectado
Vec<BlockDevice*>& disks();
int fixed_count();                  // discos internos (no extraíbles)
}

namespace ahci {
void init();
void rescan();     // nuevo intento en los puertos que aún no respondieron
bool present();    // hay alguna controladora AHCI
}
namespace ata { void init(); }

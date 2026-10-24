#include "drivers/block.h"
#include "lib/log.h"
#include "lib/string.h"

namespace {
Vec<BlockDevice*> disk_list;
}

void block::add_disk(BlockDevice* d) { disk_list.push(d); }
void block::remove_disk(BlockDevice* d) { disk_list.remove(d); }
Vec<BlockDevice*>& block::disks() { return disk_list; }
int block::fixed_count() {
    int n = 0;
    for (BlockDevice* d : disk_list) if (!d->removable) n++;
    return n;
}

Partition::Partition(BlockDevice* disk, uint64_t start, uint64_t count, int index) {
    parent = disk;
    offset = start;
    sectors = count;
    ksnprintf(name, sizeof name, "%sp%d", disk->name, index);
    strlcpy(model, disk->model, sizeof model);
}

bool Partition::read(uint64_t lba, uint32_t count, void* buf) {
    if (lba + count > sectors) return false;
    return parent->read(offset + lba, count, buf);
}

bool Partition::write(uint64_t lba, uint32_t count, const void* buf) {
    if (lba + count > sectors) return false;
    return parent->write(offset + lba, count, buf);
}

RamDisk::RamDisk(uint64_t n) {
    sectors = n;
    data = (uint8_t*)mm::kcalloc(n * 512);
    strlcpy(name, "RAM", sizeof name);
    strlcpy(model, "Disco en memoria", sizeof model);
}

bool RamDisk::read(uint64_t lba, uint32_t count, void* buf) {
    if (lba + count > sectors) return false;
    memcpy(buf, data + lba * 512, count * 512);
    return true;
}

bool RamDisk::write(uint64_t lba, uint32_t count, const void* buf) {
    if (lba + count > sectors) return false;
    memcpy(data + lba * 512, buf, count * 512);
    return true;
}

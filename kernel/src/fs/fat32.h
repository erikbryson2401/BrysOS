#pragma once
#include "drivers/block.h"
#include "lib/vec.h"

constexpr uint8_t FAT_RO = 0x01, FAT_HIDDEN = 0x02, FAT_SYSTEM = 0x04, FAT_VOLID = 0x08, FAT_DIR = 0x10,
                  FAT_ARCHIVE = 0x20, FAT_LFN = 0x0F;

// Entrada de directorio ya decodificada (nombre largo incluido)
struct FatEntry {
    String name;
    char short_name[13] = {};
    uint8_t attr = 0;
    uint32_t cluster = 0;
    uint32_t size = 0;
    uint16_t cdate = 0, ctime = 0, mdate = 0, mtime = 0;
    // Ubicación (para modificarla o borrarla)
    uint32_t dir_cluster = 0;   // primer clúster del directorio que la contiene
    uint32_t index = 0;         // índice de la entrada corta dentro del directorio
    uint32_t lfn_count = 0;     // entradas LFN que la preceden
    bool is_root = false;
    bool is_dir() const { return attr & FAT_DIR; }
};

class Fat32 {
public:
    BlockDevice* dev = nullptr;
    bool read_only = false;
    char label[12] = {};
    uint32_t serial = 0;
    uint32_t sec_per_clus = 0, reserved = 0, num_fats = 0, fat_size = 0, root_cluster = 0;
    uint32_t total_sectors = 0, fsinfo_sector = 0, data_start = 0, cluster_count = 0, cluster_bytes = 0;
    uint32_t free_count = 0xFFFFFFFF, next_free = 2;

    static Fat32* mount(BlockDevice* dev, bool read_only);
    static bool format(BlockDevice* dev, const char* label);
    static bool looks_like_fat32(const uint8_t* bootsector);

    // Operaciones (rutas relativas a la raíz, separadas por '\')
    bool resolve(const char* path, FatEntry& out);
    bool list(const FatEntry& dir, Vec<FatEntry>& out);
    bool read(const FatEntry& f, uint8_t* buf, uint32_t offset, uint32_t len);
    bool write_all(FatEntry& f, const void* data, uint32_t len);
    bool create(const FatEntry& dir, const char* name, uint8_t attr, FatEntry& out);
    bool remove(const FatEntry& e);           // archivo o directorio vacío
    bool rename(const FatEntry& e, const FatEntry& new_dir, const char* new_name);
    bool set_attr(FatEntry& e, uint8_t attr);
    bool set_label(const char* label);
    uint64_t free_bytes();
    uint64_t total_bytes() const { return (uint64_t)cluster_count * cluster_bytes; }
    void sync();

private:
    // Caché de sectores de la FAT (escritura diferida hasta sync)
    struct CacheEnt { uint32_t sector = 0xFFFFFFFF; bool dirty = false; uint8_t data[512]; };
    CacheEnt cache[32];
    uint32_t cache_next = 0;
    CacheEnt* fat_sector(uint32_t s);

    uint32_t fat_get(uint32_t c);
    void fat_set(uint32_t c, uint32_t v);
    uint32_t alloc_cluster(uint32_t prev, bool zero);
    void free_chain(uint32_t c);
    bool chain(uint32_t first, Vec<uint32_t>& out);
    uint64_t cluster_lba(uint32_t c) const { return data_start + (uint64_t)(c - 2) * sec_per_clus; }
    bool read_cluster(uint32_t c, void* buf);
    bool write_cluster(uint32_t c, const void* buf);
    bool load_dir(uint32_t first, Vec<uint8_t>& raw, Vec<uint32_t>& clusters);
    bool write_dir_entries(const Vec<uint32_t>& clusters, uint32_t index, const uint8_t* ents, uint32_t n);
    bool lookup(uint32_t dir_cluster, const char* name, FatEntry& out);
    bool make_short_name(uint32_t dir_cluster, const char* name, char out[11], bool& need_lfn);
    bool insert_entry(uint32_t dir_cluster, const char* name, const uint8_t short_ent[32], FatEntry& out);
    bool mark_deleted(const FatEntry& e);
    bool update_short(const FatEntry& e);
};

void fat_short_to_string(const uint8_t* ent, char out[13]);

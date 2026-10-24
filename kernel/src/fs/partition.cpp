// Tablas de particiones MBR (con lógicas) y GPT: lectura, espacio libre y creación
#include "fs/partition.h"
#include "fs/fat32.h"
#include "arch/idt.h"
#include "lib/log.h"
#include "lib/string.h"
#include "lib/i18n.h"

namespace {
constexpr uint64_t ALIGN = 2048;   // alineación de 1 MiB
const uint8_t GUID_BASIC_DATA[16] = {0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44, 0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7};
const uint8_t GUID_ESP[16] = {0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11, 0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B};

uint32_t rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
uint64_t rd64(const uint8_t* p) { return rd32(p) | ((uint64_t)rd32(p + 4) << 32); }
void wr32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }
void wr64(uint8_t* p, uint64_t v) { for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8 * i)); }

bool all_zero(const uint8_t* p, size_t n) {
    for (size_t i = 0; i < n; i++) if (p[i]) return false;
    return true;
}

uint32_t rng_state = 0;
uint32_t rnd() {
    if (!rng_state) rng_state = (uint32_t)pit::ticks() * 2654435761u + 0x9E3779B9;
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

void detect_fs(BlockDevice* disk, PartInfo& p) {
    if (p.mbr_type == 0x05 || p.mbr_type == 0x0F || p.mbr_type == 0x85) { p.fs = FsType::EXTENDED; return; }
    uint8_t s[1536];
    if (p.count < 3 || !disk->read(p.start, 3, s)) return;
    if (all_zero(s, 512) && all_zero(s + 1024, 512)) { p.fs = FsType::EMPTY; return; }
    if (!memcmp(s + 3, "NTFS    ", 8)) {
        p.fs = FsType::NTFS;
        uint64_t serial = rd64(s + 0x48);
        ksnprintf(p.uuid, sizeof p.uuid, "%016lX", serial);
        return;
    }
    if (!memcmp(s + 3, "EXFAT   ", 8)) { p.fs = FsType::EXFAT; return; }
    if (Fat32::looks_like_fat32(s)) {
        p.fs = FsType::FAT32;
        p.fat_serial = rd32(s + 67);
        ksnprintf(p.uuid, sizeof p.uuid, "%04X-%04X", p.fat_serial >> 16, p.fat_serial & 0xFFFF);
        Partition part(disk, p.start, p.count, p.number);
        if (Fat32* fs = Fat32::mount(&part, true)) {
            strlcpy(p.label, fs->label, sizeof p.label);
            FatEntry e;
            p.has_brysos = fs->resolve("\\brysos\\kernel.elf", e);
            delete fs;
        }
        return;
    }
    if (s[510] == 0x55 && s[511] == 0xAA && (!memcmp(s + 54, "FAT1", 4))) {
        p.fs = FsType::FAT16;
        memcpy(p.label, s + 43, 11);
        p.label[11] = 0;
        return;
    }
    // ext2/3/4: superbloque en el byte 1024
    if (s[1024 + 56] == 0x53 && s[1024 + 57] == 0xEF) {
        p.fs = FsType::EXT;
        const uint8_t* u = s + 1024 + 104;
        ksnprintf(p.uuid, sizeof p.uuid, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                  u[0], u[1], u[2], u[3], u[4], u[5], u[6], u[7], u[8], u[9], u[10], u[11], u[12], u[13], u[14], u[15]);
        memcpy(p.label, s + 1024 + 120, 16);
        p.label[16] = 0;
        return;
    }
    if (p.mbr_type == 0x82) p.fs = FsType::SWAP;
}

void compute_free(DiskLayout& l) {
    l.free.clear();
    // zonas ocupadas: primarias (incluida la extendida completa) o entradas GPT
    Vec<FreeSpace> used;
    for (auto& p : l.parts)
        if (!p.logical) used.push({p.start, p.count});
    used.sort([](const FreeSpace& a, const FreeSpace& b) { return a.start < b.start ? -1 : (a.start > b.start ? 1 : 0); });
    uint64_t pos = l.first_usable;
    auto add = [&](uint64_t s, uint64_t e) {
        s = (s + ALIGN - 1) / ALIGN * ALIGN;
        if (e > s && e - s >= 16 * 2048) l.free.push({s, e - s});   // al menos 16 MiB
    };
    for (auto& u : used) {
        if (u.start > pos) add(pos, u.start);
        if (u.start + u.count > pos) pos = u.start + u.count;
    }
    add(pos, l.last_usable + 1);
}

bool write_gpt(DiskLayout& l, uint8_t* hdr, uint8_t* entries, uint32_t ent_bytes) {
    BlockDevice* d = l.disk;
    uint32_t ent_sectors = (ent_bytes + 511) / 512;
    wr32(hdr + 88, part::crc32(entries, ent_bytes));
    uint32_t hsize = rd32(hdr + 12);
    wr32(hdr + 16, 0);
    wr32(hdr + 16, part::crc32(hdr, hsize));
    if (!d->write(rd64(hdr + 72), ent_sectors, entries) || !d->write(1, 1, hdr)) return false;
    // copia de seguridad al final del disco
    uint64_t alt = rd64(hdr + 32);
    if (alt && alt < d->sectors) {
        uint8_t bh[512];
        memcpy(bh, hdr, 512);
        uint64_t bent = alt - ent_sectors;
        wr64(bh + 24, alt);
        wr64(bh + 32, 1);
        wr64(bh + 72, bent);
        wr32(bh + 16, 0);
        wr32(bh + 16, part::crc32(bh, hsize));
        d->write(bent, ent_sectors, entries);
        d->write(alt, 1, bh);
    }
    return d->flush();
}

bool load_gpt(BlockDevice* d, uint8_t* hdr, uint8_t*& entries, uint32_t& ent_bytes) {
    if (!d->read(1, 1, hdr) || memcmp(hdr, "EFI PART", 8)) return false;
    uint32_t n = rd32(hdr + 80), sz = rd32(hdr + 84);
    if (sz < 128 || sz > 512 || n > 1024) return false;
    ent_bytes = n * sz;
    entries = (uint8_t*)mm::kcalloc(((ent_bytes + 511) / 512) * 512);
    if (!d->read(rd64(hdr + 72), (ent_bytes + 511) / 512, entries)) { mm::kfree(entries); return false; }
    return true;
}
}

uint32_t part::crc32(const void* data, size_t len, uint32_t crc) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    crc = ~crc;
    auto* p = (const uint8_t*)data;
    for (size_t i = 0; i < len; i++) crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

const char* part::fs_name(FsType t) {
    switch (t) {
    case FsType::EMPTY: return "Vacía";
    case FsType::FAT32: return "FAT32";
    case FsType::FAT16: return "FAT16";
    case FsType::EXFAT: return "exFAT";
    case FsType::NTFS: return "NTFS";
    case FsType::EXT: return "ext2/3/4";
    case FsType::SWAP: return "Intercambio";
    case FsType::EXTENDED: return "Extendida";
    default: return "Desconocido";
    }
}

bool part::read_layout(BlockDevice* d, DiskLayout& l) {
    l = DiskLayout();
    l.disk = d;
    uint8_t mbr[512];
    if (!d->read(0, 1, mbr)) return false;
    l.last_usable = min<uint64_t>(d->sectors, 0xFFFFFFFFull) - 1;
    if (all_zero(mbr, 512) || mbr[510] != 0x55 || mbr[511] != 0xAA) {
        l.blank = all_zero(mbr, 512);
        l.free_slots = 4;
        if (l.blank) compute_free(l);
        return true;
    }
    if (Fat32::looks_like_fat32(mbr)) return true; // disco sin tabla con FAT32 directa: no se toca
    for (int i = 0; i < 4; i++) {
        uint8_t* e = mbr + 446 + i * 16;
        if (e[4] == 0xEE) l.gpt = true;
    }
    if (l.gpt) {
        uint8_t hdr[512];
        uint8_t* ents;
        uint32_t eb;
        if (!load_gpt(d, hdr, ents, eb)) return false;
        l.first_usable = max<uint64_t>(rd64(hdr + 40), ALIGN);
        l.last_usable = rd64(hdr + 48);
        uint32_t sz = rd32(hdr + 84);
        for (uint32_t i = 0; i < eb / sz; i++) {
            uint8_t* e = ents + i * sz;
            if (all_zero(e, 16)) continue;
            PartInfo p;
            p.number = (int)i + 1;
            p.slot = (int)i;
            memcpy(p.gpt_type, e, 16);
            p.start = rd64(e + 32);
            p.count = rd64(e + 40) - p.start + 1;
            p.esp = !memcmp(e, GUID_ESP, 16);
            detect_fs(d, p);
            l.parts.push(p);
        }
        mm::kfree(ents);
        compute_free(l);
        return true;
    }
    int logical_no = 5;
    for (int i = 0; i < 4; i++) {
        uint8_t* e = mbr + 446 + i * 16;
        uint32_t start = rd32(e + 8), count = rd32(e + 12);
        if (!e[4] || !count) { l.free_slots++; continue; }
        PartInfo p;
        p.number = i + 1;
        p.slot = i;
        p.mbr_type = e[4];
        p.active = e[0] == 0x80;
        p.start = start;
        p.count = count;
        p.esp = e[4] == 0xEF;
        detect_fs(d, p);
        l.parts.push(p);
        if (p.fs == FsType::EXTENDED) {
            // cadena de particiones lógicas (EBR)
            uint64_t ebr = start;
            for (int guard = 0; guard < 64 && ebr; guard++) {
                uint8_t b[512];
                if (!d->read(ebr, 1, b) || b[510] != 0x55 || b[511] != 0xAA) break;
                uint8_t* l1 = b + 446;
                uint8_t* l2 = b + 462;
                if (l1[4] && rd32(l1 + 12)) {
                    PartInfo q;
                    q.number = logical_no++;
                    q.logical = true;
                    q.mbr_type = l1[4];
                    q.start = ebr + rd32(l1 + 8);
                    q.count = rd32(l1 + 12);
                    detect_fs(d, q);
                    l.parts.push(q);
                }
                ebr = (l2[4] && rd32(l2 + 8)) ? start + rd32(l2 + 8) : 0;
            }
        }
    }
    compute_free(l);
    return true;
}

bool part::create(DiskLayout& l, uint64_t start, uint64_t count, PartInfo& out, const char*& err) {
    BlockDevice* d = l.disk;
    start = (start + ALIGN - 1) / ALIGN * ALIGN;
    count = count / ALIGN * ALIGN;
    if (count < 64 * 2048) { err = T("La partición debe tener al menos 64 MB."); return false; }
    if (l.gpt) {
        uint8_t hdr[512];
        uint8_t* ents;
        uint32_t eb;
        if (!load_gpt(d, hdr, ents, eb)) { err = T("No se puede leer la tabla GPT."); return false; }
        uint32_t sz = rd32(hdr + 84);
        int slot = -1;
        for (uint32_t i = 0; i < eb / sz; i++) if (all_zero(ents + i * sz, 16)) { slot = (int)i; break; }
        if (slot < 0) { mm::kfree(ents); err = T("La tabla GPT está llena."); return false; }
        uint8_t* e = ents + slot * sz;
        memset(e, 0, sz);
        memcpy(e, GUID_BASIC_DATA, 16);
        for (int k = 16; k < 32; k += 4) wr32(e + k, rnd());
        e[16 + 7] = (uint8_t)((e[16 + 7] & 0x0F) | 0x40);  // GUID versión 4
        e[16 + 8] = (uint8_t)((e[16 + 8] & 0x3F) | 0x80);
        wr64(e + 32, start);
        wr64(e + 40, start + count - 1);
        const char* name = "BrysOS";
        for (int k = 0; name[k]; k++) e[56 + k * 2] = (uint8_t)name[k];
        bool ok = write_gpt(l, hdr, ents, eb);
        mm::kfree(ents);
        if (!ok) { err = T("No se pudo escribir la tabla GPT."); return false; }
        out = PartInfo();
        out.number = slot + 1;
        out.slot = slot;
        out.start = start;
        out.count = count;
        memcpy(out.gpt_type, GUID_BASIC_DATA, 16);
        return true;
    }
    uint8_t mbr[512];
    if (!d->read(0, 1, mbr)) { err = T("No se puede leer el MBR."); return false; }
    if (l.blank) {
        memset(mbr, 0, 512);
        wr32(mbr + 440, rnd());
        mbr[510] = 0x55;
        mbr[511] = 0xAA;
    }
    if (start + count > 0xFFFFFFFFull) { err = T("Una tabla MBR no puede direccionar más de 2 TB."); return false; }
    int slot = -1;
    for (int i = 0; i < 4; i++) {
        uint8_t* e = mbr + 446 + i * 16;
        if (!e[4] || !rd32(e + 12)) { slot = i; break; }
    }
    if (slot < 0) { err = T("El disco ya tiene cuatro particiones primarias; libere una desde otro sistema."); return false; }
    uint8_t* e = mbr + 446 + slot * 16;
    memset(e, 0, 16);
    e[1] = 0xFE; e[2] = 0xFF; e[3] = 0xFF;
    e[4] = 0x0C;
    e[5] = 0xFE; e[6] = 0xFF; e[7] = 0xFF;
    wr32(e + 8, (uint32_t)start);
    wr32(e + 12, (uint32_t)count);
    if (!d->write(0, 1, mbr) || !d->flush()) { err = T("No se pudo escribir el MBR."); return false; }
    out = PartInfo();
    out.number = slot + 1;
    out.slot = slot;
    out.start = start;
    out.count = count;
    out.mbr_type = 0x0C;
    return true;
}

bool part::set_fat32_type(DiskLayout& l, const PartInfo& p, const char*& err) {
    BlockDevice* d = l.disk;
    if (l.gpt) {
        if (p.esp) return true; // la ESP conserva su tipo
        uint8_t hdr[512];
        uint8_t* ents;
        uint32_t eb;
        if (!load_gpt(d, hdr, ents, eb)) { err = T("No se puede leer la tabla GPT."); return false; }
        memcpy(ents + p.slot * rd32(hdr + 84), GUID_BASIC_DATA, 16);
        bool ok = write_gpt(l, hdr, ents, eb);
        mm::kfree(ents);
        if (!ok) err = T("No se pudo escribir la tabla GPT.");
        return ok;
    }
    if (p.logical) {
        // el tipo de una lógica está en su EBR, en el sector anterior a la partición alineada:
        // se busca recorriendo la cadena
        for (auto& q : l.parts) {
            if (q.fs != FsType::EXTENDED) continue;
            uint64_t ebr = q.start;
            for (int guard = 0; guard < 64 && ebr; guard++) {
                uint8_t b[512];
                if (!d->read(ebr, 1, b)) break;
                if (ebr + rd32(b + 446 + 8) == p.start) {
                    b[446 + 4] = 0x0C;
                    return d->write(ebr, 1, b) && d->flush();
                }
                ebr = rd32(b + 462 + 8) ? q.start + rd32(b + 462 + 8) : 0;
            }
        }
        err = T("No se encontró la entrada de la partición lógica.");
        return false;
    }
    uint8_t mbr[512];
    if (!d->read(0, 1, mbr)) { err = T("No se puede leer el MBR."); return false; }
    mbr[446 + p.slot * 16 + 4] = 0x0C;
    if (!d->write(0, 1, mbr) || !d->flush()) { err = T("No se pudo escribir el MBR."); return false; }
    return true;
}

bool part::whole_disk(BlockDevice* d, PartInfo& out, const char*& err) {
    // borra también las cabeceras GPT (principal y de copia) para no dejar restos
    uint8_t* zero = (uint8_t*)mm::kcalloc(34 * 512);
    d->write(0, 34, zero);
    if (d->sectors > 34) d->write(d->sectors - 33, 33, zero);
    mm::kfree(zero);
    DiskLayout l;
    l.disk = d;
    l.blank = true;
    l.free_slots = 4;
    l.last_usable = min<uint64_t>(d->sectors, 0xFFFFFFFFull) - 1;
    return create(l, ALIGN, l.last_usable + 1 - ALIGN, out, err);
}

Partition* part::open(BlockDevice* d, const PartInfo& p) { return new Partition(d, p.start, p.count, p.number); }

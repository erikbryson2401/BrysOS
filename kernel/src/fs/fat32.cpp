// Sistema de archivos FAT32 con nombres largos (VFAT): lectura y escritura
#include "fs/fat32.h"
#include "drivers/rtc.h"
#include "lib/log.h"
#include "lib/string.h"
#include "arch/idt.h"

bool strchr_simple(const char* s, char c);

namespace {
constexpr uint32_t EOC = 0x0FFFFFFF;
constexpr uint32_t MASK = 0x0FFFFFFF;

inline uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
inline uint32_t rd32(const uint8_t* p) { return (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24)); }
inline void wr16(uint8_t* p, uint16_t v) { p[0] = v & 0xFF; p[1] = v >> 8; }
inline void wr32(uint8_t* p, uint32_t v) { p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; p[2] = (v >> 16) & 0xFF; p[3] = v >> 24; }

const int lfn_offsets[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};

uint8_t lfn_checksum(const uint8_t* name11) {
    uint8_t sum = 0;
    for (int i = 0; i < 11; i++) sum = (uint8_t)(((sum & 1) << 7) + (sum >> 1) + name11[i]);
    return sum;
}

bool valid_short_char(char c) {
    if (c >= 'A' && c <= 'Z') return true;
    if (c >= '0' && c <= '9') return true;
    return strchr_simple("!#$%&'()-@^_`{}~", c);
}
}

// strchr sencillo (no hay libc)
bool strchr_simple(const char* s, char c) {
    for (; *s; s++) if (*s == c) return true;
    return false;
}

void fat_short_to_string(const uint8_t* e, char out[13]) {
    int n = 0;
    bool lower_base = e[12] & 0x08, lower_ext = e[12] & 0x10;
    for (int i = 0; i < 8 && e[i] != ' '; i++) {
        char c = (char)(i == 0 && e[0] == 0x05 ? 0xE5 : e[i]);
        out[n++] = lower_base ? to_lower(c) : c;
    }
    if (e[8] != ' ') {
        out[n++] = '.';
        for (int i = 8; i < 11 && e[i] != ' '; i++) out[n++] = lower_ext ? to_lower((char)e[i]) : (char)e[i];
    }
    out[n] = 0;
}

// ---------------------------------------------------------------------------
// Montaje
// ---------------------------------------------------------------------------
bool Fat32::looks_like_fat32(const uint8_t* b) {
    if (b[510] != 0x55 || b[511] != 0xAA) return false;
    if (rd16(b + 11) != 512) return false;
    uint8_t spc = b[13];
    if (!spc || (spc & (spc - 1))) return false;
    if (rd16(b + 17) != 0 || rd16(b + 22) != 0) return false; // sin raíz fija y FAT de 32 bits
    if (b[16] == 0 || rd32(b + 36) == 0) return false;
    return true;
}

Fat32* Fat32::mount(BlockDevice* dev, bool ro) {
    uint8_t bs[512];
    if (!dev->read(0, 1, bs) || !looks_like_fat32(bs)) return nullptr;
    auto* f = new Fat32();
    f->dev = dev;
    f->read_only = ro;
    f->sec_per_clus = bs[13];
    f->reserved = rd16(bs + 14);
    f->num_fats = bs[16];
    f->total_sectors = rd16(bs + 19) ? rd16(bs + 19) : rd32(bs + 32);
    f->fat_size = rd32(bs + 36);
    f->root_cluster = rd32(bs + 44);
    f->fsinfo_sector = rd16(bs + 48);
    f->data_start = f->reserved + f->num_fats * f->fat_size;
    f->cluster_count = (f->total_sectors - f->data_start) / f->sec_per_clus;
    f->cluster_bytes = f->sec_per_clus * 512;
    if (f->cluster_count < 2 || f->total_sectors > dev->sectors + 64) { delete f; return nullptr; }
    memcpy(f->label, bs + 71, 11);
    f->label[11] = 0;
    f->serial = rd32(bs + 67);
    // FSInfo
    uint8_t fi[512];
    if (f->fsinfo_sector && f->fsinfo_sector < f->reserved && dev->read(f->fsinfo_sector, 1, fi) &&
        rd32(fi) == 0x41615252 && rd32(fi + 484) == 0x61417272) {
        f->free_count = rd32(fi + 488);
        f->next_free = rd32(fi + 492);
        if (f->next_free < 2 || f->next_free >= f->cluster_count + 2) f->next_free = 2;
        if (f->free_count > f->cluster_count) f->free_count = 0xFFFFFFFF;
    }
    // Etiqueta desde la entrada de volumen del directorio raíz (más fiable)
    Vec<uint8_t> raw;
    Vec<uint32_t> cl;
    if (f->load_dir(f->root_cluster, raw, cl)) {
        for (size_t i = 0; i + 32 <= raw.size(); i += 32) {
            uint8_t* e = &raw[i];
            if (e[0] == 0) break;
            if (e[0] == 0xE5 || e[11] == FAT_LFN) continue;
            if ((e[11] & FAT_VOLID) && !(e[11] & FAT_DIR)) {
                memcpy(f->label, e, 11);
                f->label[11] = 0;
                break;
            }
        }
    }
    for (int i = 10; i >= 0 && f->label[i] == ' '; i--) f->label[i] = 0;
    if (!strcmp(f->label, "NO NAME")) f->label[0] = 0;
    return f;
}

// ---------------------------------------------------------------------------
// FAT
// ---------------------------------------------------------------------------
Fat32::CacheEnt* Fat32::fat_sector(uint32_t s) {
    for (auto& c : cache)
        if (c.sector == s) return &c;
    CacheEnt* c = &cache[cache_next];
    cache_next = (cache_next + 1) % 32;
    if (c->dirty) {
        for (uint32_t k = 0; k < num_fats; k++) dev->write(reserved + k * fat_size + c->sector, 1, c->data);
        c->dirty = false;
    }
    c->sector = s;
    if (!dev->read(reserved + s, 1, c->data)) memset(c->data, 0, 512);
    return c;
}

uint32_t Fat32::fat_get(uint32_t c) {
    if (c < 2 || c >= cluster_count + 2) return EOC;
    CacheEnt* e = fat_sector(c / 128);
    return rd32(e->data + (c % 128) * 4) & MASK;
}

void Fat32::fat_set(uint32_t c, uint32_t v) {
    if (c < 2 || c >= cluster_count + 2) return;
    CacheEnt* e = fat_sector(c / 128);
    uint8_t* p = e->data + (c % 128) * 4;
    wr32(p, (rd32(p) & ~MASK) | (v & MASK));
    e->dirty = true;
}

uint32_t Fat32::alloc_cluster(uint32_t prev, bool zero) {
    uint32_t total = cluster_count;
    uint32_t c = next_free;
    for (uint32_t n = 0; n < total; n++, c++) {
        if (c >= total + 2) c = 2;
        if (fat_get(c) == 0) {
            fat_set(c, EOC);
            if (prev) fat_set(prev, c);
            next_free = c + 1;
            if (free_count != 0xFFFFFFFF && free_count) free_count--;
            if (zero) {
                auto* z = (uint8_t*)mm::kcalloc(cluster_bytes);
                write_cluster(c, z);
                mm::kfree(z);
            }
            return c;
        }
    }
    return 0;
}

void Fat32::free_chain(uint32_t c) {
    int guard = 0;
    while (c >= 2 && c < cluster_count + 2 && guard++ < 0x1000000) {
        uint32_t n = fat_get(c);
        fat_set(c, 0);
        if (free_count != 0xFFFFFFFF) free_count++;
        if (c < next_free) next_free = c;
        if (n >= 0x0FFFFFF8 || n < 2) break;
        c = n;
    }
}

bool Fat32::chain(uint32_t first, Vec<uint32_t>& out) {
    out.clear();
    uint32_t c = first;
    while (c >= 2 && c < cluster_count + 2) {
        out.push(c);
        if (out.size() > cluster_count) return false; // bucle en la FAT
        c = fat_get(c);
        if (c >= 0x0FFFFFF8) break;
    }
    return true;
}

bool Fat32::read_cluster(uint32_t c, void* buf) { return dev->read(cluster_lba(c), sec_per_clus, buf); }
bool Fat32::write_cluster(uint32_t c, const void* buf) {
    if (read_only) return false;
    return dev->write(cluster_lba(c), sec_per_clus, buf);
}

void Fat32::sync() {
    if (read_only) return;
    for (auto& c : cache) {
        if (c.dirty) {
            for (uint32_t k = 0; k < num_fats; k++) dev->write(reserved + k * fat_size + c.sector, 1, c.data);
            c.dirty = false;
        }
    }
    uint8_t fi[512];
    if (fsinfo_sector && dev->read(fsinfo_sector, 1, fi) && rd32(fi) == 0x41615252) {
        wr32(fi + 488, free_count);
        wr32(fi + 492, next_free);
        dev->write(fsinfo_sector, 1, fi);
    }
    dev->flush();
}

uint64_t Fat32::free_bytes() {
    if (free_count == 0xFFFFFFFF) {
        // recuento completo leyendo la FAT por bloques
        uint32_t n = 0;
        auto* buf = (uint8_t*)mm::kmalloc(64 * 512);
        for (uint32_t s = 0; s < fat_size; s += 64) {
            uint32_t cnt = min(64u, fat_size - s);
            if (!dev->read(reserved + s, cnt, buf)) break;
            for (uint32_t i = 0; i < cnt * 128; i++) {
                uint32_t cl = s * 128 + i;
                if (cl < 2 || cl >= cluster_count + 2) continue;
                if ((rd32(buf + i * 4) & MASK) == 0) n++;
            }
        }
        mm::kfree(buf);
        free_count = n;
    }
    return (uint64_t)free_count * cluster_bytes;
}

// ---------------------------------------------------------------------------
// Directorios
// ---------------------------------------------------------------------------
bool Fat32::load_dir(uint32_t first, Vec<uint8_t>& raw, Vec<uint32_t>& clusters) {
    if (!chain(first, clusters) || clusters.empty()) return false;
    raw.resize(clusters.size() * cluster_bytes);
    for (size_t i = 0; i < clusters.size(); i++)
        if (!read_cluster(clusters[i], raw.data() + i * cluster_bytes)) return false;
    return true;
}

bool Fat32::list(const FatEntry& dir, Vec<FatEntry>& out) {
    out.clear();
    uint32_t first = dir.is_root ? root_cluster : dir.cluster;
    Vec<uint8_t> raw;
    Vec<uint32_t> cl;
    if (!load_dir(first, raw, cl)) return false;
    char lfn[264];
    int lfn_len = 0;
    uint8_t lfn_sum = 0;
    int lfn_expect = 0;
    uint32_t lfn_entries = 0;
    bool lfn_ok = false;
    size_t n = raw.size() / 32;
    for (size_t i = 0; i < n; i++) {
        uint8_t* e = &raw[i * 32];
        if (e[0] == 0) break;
        if (e[0] == 0xE5) { lfn_ok = false; lfn_entries = 0; continue; }
        if (e[11] == FAT_LFN) {
            int ord = e[0] & 0x1F;
            if (e[0] & 0x40) {
                lfn_expect = ord;
                lfn_sum = e[13];
                lfn_ok = ord >= 1 && ord <= 20;
                lfn_entries = 0;
                lfn_len = ord * 13;
                memset(lfn, 0, sizeof lfn);
            } else if (!lfn_ok || ord != lfn_expect || e[13] != lfn_sum) {
                lfn_ok = false;
            }
            if (lfn_ok) {
                lfn_expect = ord - 1;
                lfn_entries++;
                for (int k = 0; k < 13; k++) {
                    uint16_t ch = rd16(e + lfn_offsets[k]);
                    int pos = (ord - 1) * 13 + k;
                    if (ch == 0x0000 || ch == 0xFFFF) { if (pos < lfn_len) lfn_len = pos; continue; }
                    if (pos < 260) lfn[pos] = ch < 256 ? (char)ch : '?';
                }
            }
            continue;
        }
        if (e[11] & FAT_VOLID) { lfn_ok = false; lfn_entries = 0; continue; }
        FatEntry fe;
        fat_short_to_string(e, fe.short_name);
        bool use_lfn = lfn_ok && lfn_expect == 0 && lfn_sum == lfn_checksum(e);
        if (use_lfn) {
            lfn[min(lfn_len, 260)] = 0;
            fe.name = lfn;
            fe.lfn_count = lfn_entries;
        } else {
            fe.name = fe.short_name;
            fe.lfn_count = 0;
        }
        lfn_ok = false;
        lfn_entries = 0;
        if (!strcmp(fe.short_name, ".") || !strcmp(fe.short_name, "..")) continue;
        fe.attr = e[11];
        fe.cluster = ((uint32_t)rd16(e + 20) << 16) | rd16(e + 26);
        fe.size = rd32(e + 28);
        fe.ctime = rd16(e + 14);
        fe.cdate = rd16(e + 16);
        fe.mtime = rd16(e + 22);
        fe.mdate = rd16(e + 24);
        fe.dir_cluster = first;
        fe.index = (uint32_t)i;
        out.push(move_val(fe));
    }
    return true;
}

bool Fat32::lookup(uint32_t dir_cluster, const char* name, FatEntry& out) {
    FatEntry d;
    d.cluster = dir_cluster;
    d.is_root = dir_cluster == root_cluster;
    d.attr = FAT_DIR;
    Vec<FatEntry> ents;
    if (!list(d, ents)) return false;
    for (auto& e : ents) {
        if (!stricmp(e.name.c_str(), name) || !stricmp(e.short_name, name)) {
            out = e;
            return true;
        }
    }
    return false;
}

bool Fat32::resolve(const char* path, FatEntry& out) {
    FatEntry cur;
    cur.is_root = true;
    cur.cluster = root_cluster;
    cur.attr = FAT_DIR;
    cur.name = "";
    const char* p = path;
    char comp[264];
    while (*p) {
        while (*p == '\\' || *p == '/') p++;
        if (!*p) break;
        int n = 0;
        while (*p && *p != '\\' && *p != '/' && n < 260) comp[n++] = *p++;
        comp[n] = 0;
        if (!cur.is_dir()) return false;
        FatEntry next;
        if (!lookup(cur.is_root ? root_cluster : cur.cluster, comp, next)) return false;
        cur = next;
    }
    out = cur;
    return true;
}

bool Fat32::read(const FatEntry& f, uint8_t* buf, uint32_t offset, uint32_t len) {
    if (offset >= f.size) return len == 0;
    if (offset + len > f.size) len = f.size - offset;
    Vec<uint32_t> cl;
    if (!chain(f.cluster, cl)) return false;
    uint32_t first = offset / cluster_bytes;
    uint8_t* tmp = nullptr;
    uint32_t done = 0;
    for (size_t i = first; i < cl.size() && done < len; i++) {
        uint32_t cstart = (uint32_t)i * cluster_bytes;
        uint32_t from = offset + done - cstart;
        uint32_t n = min(cluster_bytes - from, len - done);
        if (from == 0 && n == cluster_bytes) {
            // agrupa clústeres contiguos en una sola lectura
            size_t run = 1;
            while (i + run < cl.size() && cl[i + run] == cl[i] + run && (run + 1) * cluster_bytes <= len - done &&
                   run < 64)
                run++;
            if (!dev->read(cluster_lba(cl[i]), (uint32_t)(sec_per_clus * run), buf + done)) { mm::kfree(tmp); return false; }
            done += (uint32_t)(run * cluster_bytes);
            i += run - 1;
            continue;
        }
        if (!tmp) tmp = (uint8_t*)mm::kmalloc(cluster_bytes);
        if (!read_cluster(cl[i], tmp)) { mm::kfree(tmp); return false; }
        memcpy(buf + done, tmp + from, n);
        done += n;
    }
    mm::kfree(tmp);
    return done == len;
}

bool Fat32::write_dir_entries(const Vec<uint32_t>& clusters, uint32_t index, const uint8_t* ents, uint32_t n) {
    uint8_t sec[512];
    uint64_t cur_lba = ~0ull;
    for (uint32_t k = 0; k < n; k++) {
        uint32_t byte = (index + k) * 32;
        uint32_t ci = byte / cluster_bytes;
        if (ci >= clusters.size()) return false;
        uint32_t off = byte % cluster_bytes;
        uint64_t lba = cluster_lba(clusters[ci]) + off / 512;
        if (lba != cur_lba) {
            if (cur_lba != ~0ull && !dev->write(cur_lba, 1, sec)) return false;
            if (!dev->read(lba, 1, sec)) return false;
            cur_lba = lba;
        }
        memcpy(sec + off % 512, ents + k * 32, 32);
    }
    if (cur_lba != ~0ull) return dev->write(cur_lba, 1, sec);
    return true;
}

bool Fat32::make_short_name(uint32_t dir_cluster, const char* name, char out[11], bool& need_lfn) {
    memset(out, ' ', 11);
    int len = (int)strlen(name);
    int dot = -1;
    for (int i = len - 1; i > 0; i--) if (name[i] == '.') { dot = i; break; }
    int base_len = dot < 0 ? len : dot;
    int ext_len = dot < 0 ? 0 : len - dot - 1;
    // ¿Ya es un nombre 8.3 válido en mayúsculas?
    bool valid = base_len >= 1 && base_len <= 8 && ext_len <= 3;
    for (int i = 0; i < len && valid; i++) {
        if (i == dot) continue;
        if (!valid_short_char(name[i])) valid = false;
    }
    if (valid) {
        for (int i = 0; i < base_len; i++) out[i] = name[i];
        for (int i = 0; i < ext_len; i++) out[8 + i] = name[dot + 1 + i];
        need_lfn = false;
        return true;
    }
    need_lfn = true;
    char basis[8];
    int bn = 0;
    for (int i = 0; i < base_len && bn < 8; i++) {
        char c = to_upper(name[i]);
        if (c == ' ' || c == '.') continue;
        basis[bn++] = valid_short_char(c) ? c : '_';
    }
    if (bn == 0) basis[bn++] = '_';
    int en = 0;
    for (int i = 0; i < ext_len && en < 3; i++) {
        char c = to_upper(name[dot + 1 + i]);
        if (c == ' ') continue;
        out[8 + en++] = valid_short_char(c) ? c : '_';
    }
    // Nombres cortos existentes
    Vec<uint8_t> raw;
    Vec<uint32_t> cl;
    if (!load_dir(dir_cluster, raw, cl)) return false;
    for (int n = 1; n < 1000000; n++) {
        char tail[8];
        int tl = ksnprintf(tail, sizeof tail, "~%d", n);
        int keep = min(bn, 8 - tl);
        for (int i = 0; i < 8; i++) out[i] = ' ';
        for (int i = 0; i < keep; i++) out[i] = basis[i];
        for (int i = 0; i < tl; i++) out[keep + i] = tail[i];
        bool clash = false;
        for (size_t i = 0; i + 32 <= raw.size(); i += 32) {
            if (raw[i] == 0) break;
            if (raw[i] == 0xE5 || raw[i + 11] == FAT_LFN) continue;
            if (!memcmp(&raw[i], out, 11)) { clash = true; break; }
        }
        if (!clash) return true;
    }
    return false;
}

bool Fat32::insert_entry(uint32_t dir_cluster, const char* name, const uint8_t short_ent[32], FatEntry& out) {
    bool need_lfn = !(short_ent[12] & 0x80); // bit auxiliar: 0x80 = sin LFN
    int name_len = (int)strlen(name);
    uint32_t lfn_n = need_lfn ? (uint32_t)((name_len + 12) / 13) : 0;
    uint32_t total = lfn_n + 1;
    Vec<uint8_t> raw;
    Vec<uint32_t> cl;
    if (!load_dir(dir_cluster, raw, cl)) return false;
    uint32_t n = (uint32_t)(raw.size() / 32);
    // Busca 'total' entradas libres consecutivas
    uint32_t start = 0, run = 0;
    bool found = false;
    bool at_end = false;
    for (uint32_t i = 0; i < n; i++) {
        uint8_t b = raw[i * 32];
        if (b == 0) at_end = true;
        if (at_end || b == 0xE5) {
            if (run == 0) start = i;
            if (++run == total) { found = true; break; }
        } else {
            run = 0;
        }
    }
    while (!found) {
        // amplía el directorio con un clúster vacío
        uint32_t nc = alloc_cluster(cl.back(), true);
        if (!nc) return false;
        cl.push(nc);
        uint32_t per = cluster_bytes / 32;
        if (run == 0) start = n;
        run += per;
        n += per;
        if (run >= total) found = true;
    }
    auto* ents = (uint8_t*)mm::kcalloc(total * 32);
    uint8_t sname[11];
    memcpy(sname, short_ent, 11);
    uint8_t sum = lfn_checksum(sname);
    for (uint32_t k = 0; k < lfn_n; k++) {
        uint32_t ord = lfn_n - k; // la primera entrada física es la última parte del nombre
        uint8_t* e = ents + k * 32;
        e[0] = (uint8_t)(ord | (k == 0 ? 0x40 : 0));
        e[11] = FAT_LFN;
        e[12] = 0;
        e[13] = sum;
        wr16(e + 26, 0);
        for (int c = 0; c < 13; c++) {
            int pos = (int)(ord - 1) * 13 + c;
            uint16_t ch;
            if (pos < name_len) ch = (uint8_t)name[pos];
            else if (pos == name_len) ch = 0;
            else ch = 0xFFFF;
            wr16(e + lfn_offsets[c], ch);
        }
    }
    // la entrada corta se copia aparte: 'ents' se libera antes de rellenar 'out'
    // (antes se leía de la memoria ya liberada y el primer clúster salía con basura)
    uint8_t se[32];
    memcpy(se, short_ent, 32);
    se[12] &= ~0x80;
    memcpy(ents + lfn_n * 32, se, 32);
    bool ok = write_dir_entries(cl, start, ents, total);
    mm::kfree(ents);
    if (!ok) return false;
    out.name = name;
    fat_short_to_string(se, out.short_name);
    out.attr = se[11];
    out.cluster = ((uint32_t)rd16(se + 20) << 16) | rd16(se + 26);
    out.size = rd32(se + 28);
    out.ctime = rd16(se + 14);
    out.cdate = rd16(se + 16);
    out.mtime = rd16(se + 22);
    out.mdate = rd16(se + 24);
    out.dir_cluster = dir_cluster;
    out.index = start + lfn_n;
    out.lfn_count = lfn_n;
    out.is_root = false;
    return true;
}

static bool valid_long_name(const char* name) {
    if (!name[0]) return false;
    for (const char* p = name; *p; p++) {
        uint8_t c = (uint8_t)*p;
        if (c < 32 || strchr_simple("\\/:*?\"<>|", (char)c)) return false;
    }
    int l = (int)strlen(name);
    if (name[l - 1] == ' ' || name[l - 1] == '.') return false;
    if (!strcmp(name, ".") || !strcmp(name, "..")) return false;
    return l <= 255;
}

bool Fat32::create(const FatEntry& dir, const char* name, uint8_t attr, FatEntry& out) {
    if (read_only || !dir.is_dir() || !valid_long_name(name)) return false;
    uint32_t dc = dir.is_root ? root_cluster : dir.cluster;
    FatEntry existing;
    if (lookup(dc, name, existing)) return false;
    char sn[11];
    bool need_lfn;
    if (!make_short_name(dc, name, sn, need_lfn)) return false;
    uint8_t e[32] = {};
    memcpy(e, sn, 11);
    e[11] = attr;
    if (!need_lfn) e[12] |= 0x80;
    DateTime now = rtc::now();
    wr16(e + 14, rtc::fat_time(now));
    wr16(e + 16, rtc::fat_date(now));
    wr16(e + 18, rtc::fat_date(now));
    wr16(e + 22, rtc::fat_time(now));
    wr16(e + 24, rtc::fat_date(now));
    uint32_t c = 0;
    if (attr & FAT_DIR) {
        c = alloc_cluster(0, false);
        if (!c) return false;
        auto* buf = (uint8_t*)mm::kcalloc(cluster_bytes);
        // entradas "." y ".."
        memset(buf, ' ', 11);
        buf[0] = '.';
        buf[11] = FAT_DIR;
        wr16(buf + 20, c >> 16);
        wr16(buf + 26, c & 0xFFFF);
        memcpy(buf + 14, e + 14, 4);
        memcpy(buf + 22, e + 22, 4);
        uint8_t* d2 = buf + 32;
        memset(d2, ' ', 11);
        d2[0] = '.';
        d2[1] = '.';
        d2[11] = FAT_DIR;
        uint32_t pc = dir.is_root ? 0 : dir.cluster;
        wr16(d2 + 20, pc >> 16);
        wr16(d2 + 26, pc & 0xFFFF);
        memcpy(d2 + 14, e + 14, 4);
        memcpy(d2 + 22, e + 22, 4);
        bool ok = write_cluster(c, buf);
        mm::kfree(buf);
        if (!ok) { free_chain(c); return false; }
    }
    wr16(e + 20, c >> 16);
    wr16(e + 26, c & 0xFFFF);
    bool ok = insert_entry(dc, name, e, out);
    if (!ok && c) free_chain(c);
    sync();
    return ok;
}

bool Fat32::update_short(const FatEntry& f) {
    Vec<uint32_t> cl;
    if (!chain(f.dir_cluster, cl)) return false;
    uint32_t byte = f.index * 32;
    uint32_t ci = byte / cluster_bytes;
    if (ci >= cl.size()) return false;
    uint64_t lba = cluster_lba(cl[ci]) + (byte % cluster_bytes) / 512;
    uint8_t sec[512];
    if (!dev->read(lba, 1, sec)) return false;
    uint8_t* e = sec + byte % 512;
    e[11] = f.attr;
    wr16(e + 20, f.cluster >> 16);
    wr16(e + 26, f.cluster & 0xFFFF);
    wr32(e + 28, f.is_dir() ? 0 : f.size);
    wr16(e + 22, f.mtime);
    wr16(e + 24, f.mdate);
    wr16(e + 18, f.mdate);
    return dev->write(lba, 1, sec);
}

bool Fat32::write_all(FatEntry& f, const void* data, uint32_t len) {
    if (read_only || f.is_dir()) return false;
    uint32_t need = (len + cluster_bytes - 1) / cluster_bytes;
    Vec<uint32_t> cl;
    if (!chain(f.cluster, cl)) return false;
    if (need == 0) {
        if (f.cluster) free_chain(f.cluster);
        f.cluster = 0;
        cl.clear();
    } else if (cl.size() > need) {
        free_chain(cl[need]);
        fat_set(cl[need - 1], EOC);
        cl.resize(need);
    } else {
        while (cl.size() < need) {
            uint32_t c = alloc_cluster(cl.empty() ? 0 : cl.back(), false);
            if (!c) { sync(); return false; } // disco lleno
            if (cl.empty()) f.cluster = c;
            cl.push(c);
        }
    }
    auto* src = (const uint8_t*)data;
    uint8_t* tmp = nullptr;
    uint32_t done = 0;
    for (size_t i = 0; i < cl.size(); i++) {
        uint32_t remain = len - done;
        if (remain >= cluster_bytes) {
            size_t run = 1;
            while (i + run < cl.size() && cl[i + run] == cl[i] + run && (run + 1) * cluster_bytes <= remain && run < 64)
                run++;
            if (!dev->write(cluster_lba(cl[i]), (uint32_t)(sec_per_clus * run), src + done)) { mm::kfree(tmp); return false; }
            done += (uint32_t)(run * cluster_bytes);
            i += run - 1;
        } else {
            if (!tmp) tmp = (uint8_t*)mm::kcalloc(cluster_bytes);
            memcpy(tmp, src + done, remain);
            if (!write_cluster(cl[i], tmp)) { mm::kfree(tmp); return false; }
            done += remain;
        }
    }
    mm::kfree(tmp);
    DateTime now = rtc::now();
    f.size = len;
    f.mdate = rtc::fat_date(now);
    f.mtime = rtc::fat_time(now);
    f.attr |= FAT_ARCHIVE;
    bool ok = update_short(f);
    sync();
    return ok;
}

bool Fat32::mark_deleted(const FatEntry& e) {
    Vec<uint32_t> cl;
    if (!chain(e.dir_cluster, cl)) return false;
    uint32_t first = e.index - e.lfn_count;
    uint8_t sec[512];
    uint64_t cur = ~0ull;
    for (uint32_t i = first; i <= e.index; i++) {
        uint32_t byte = i * 32;
        uint32_t ci = byte / cluster_bytes;
        if (ci >= cl.size()) return false;
        uint64_t lba = cluster_lba(cl[ci]) + (byte % cluster_bytes) / 512;
        if (lba != cur) {
            if (cur != ~0ull && !dev->write(cur, 1, sec)) return false;
            if (!dev->read(lba, 1, sec)) return false;
            cur = lba;
        }
        sec[byte % 512] = 0xE5;
    }
    return cur == ~0ull || dev->write(cur, 1, sec);
}

bool Fat32::remove(const FatEntry& e) {
    if (read_only || e.is_root) return false;
    if (e.is_dir()) {
        Vec<FatEntry> sub;
        if (!list(e, sub) || !sub.empty()) return false;
    }
    if (!mark_deleted(e)) return false;
    if (e.cluster) free_chain(e.cluster);
    sync();
    return true;
}

bool Fat32::rename(const FatEntry& e, const FatEntry& new_dir, const char* new_name) {
    if (read_only || e.is_root || !valid_long_name(new_name)) return false;
    uint32_t dc = new_dir.is_root ? root_cluster : new_dir.cluster;
    FatEntry existing;
    if (lookup(dc, new_name, existing)) {
        // sólo se permite si es la misma entrada (cambio de mayúsculas)
        if (existing.dir_cluster != e.dir_cluster || existing.index != e.index) return false;
    }
    // Lee la entrada corta original para conservar fechas y atributos
    Vec<uint32_t> cl;
    if (!chain(e.dir_cluster, cl)) return false;
    uint32_t byte = e.index * 32;
    uint8_t sec[512];
    uint64_t lba = cluster_lba(cl[byte / cluster_bytes]) + (byte % cluster_bytes) / 512;
    if (!dev->read(lba, 1, sec)) return false;
    uint8_t ent[32];
    memcpy(ent, sec + byte % 512, 32);
    // Primero se marca la antigua como borrada para que su nombre corto quede libre
    if (!mark_deleted(e)) return false;
    char sn[11];
    bool need_lfn;
    FatEntry out;
    bool ok = make_short_name(dc, new_name, sn, need_lfn);
    if (ok) {
        memcpy(ent, sn, 11);
        ent[12] = need_lfn ? 0 : 0x80;
        ok = insert_entry(dc, new_name, ent, out);
    }
    if (!ok) {
        // intenta restaurar la entrada original
        Vec<uint32_t> cl2;
        if (chain(e.dir_cluster, cl2)) {
            uint8_t s2[512];
            if (dev->read(lba, 1, s2)) {
                memcpy(s2 + byte % 512, sec + byte % 512, 32);
                dev->write(lba, 1, s2);
            }
        }
        sync();
        return false;
    }
    // Si un directorio cambia de padre, actualiza su entrada ".."
    if (e.is_dir() && e.cluster && dc != e.dir_cluster) {
        auto* buf = (uint8_t*)mm::kmalloc(cluster_bytes);
        if (read_cluster(e.cluster, buf)) {
            uint8_t* d2 = buf + 32;
            if (d2[0] == '.' && d2[1] == '.') {
                uint32_t pc = new_dir.is_root ? 0 : new_dir.cluster;
                wr16(d2 + 20, pc >> 16);
                wr16(d2 + 26, pc & 0xFFFF);
                write_cluster(e.cluster, buf);
            }
        }
        mm::kfree(buf);
    }
    sync();
    return true;
}

bool Fat32::set_attr(FatEntry& e, uint8_t attr) {
    if (read_only || e.is_root) return false;
    e.attr = (uint8_t)((e.attr & FAT_DIR) | (attr & ~FAT_DIR & 0x27));
    bool ok = update_short(e);
    sync();
    return ok;
}

bool Fat32::set_label(const char* lbl) {
    if (read_only) return false;
    char l[11];
    memset(l, ' ', 11);
    for (int i = 0; i < 11 && lbl[i]; i++) l[i] = to_upper(lbl[i]);
    // sector de arranque
    uint8_t bs[512];
    if (!dev->read(0, 1, bs)) return false;
    memcpy(bs + 71, l, 11);
    dev->write(0, 1, bs);
    // entrada de volumen en la raíz
    Vec<uint8_t> raw;
    Vec<uint32_t> cl;
    if (load_dir(root_cluster, raw, cl)) {
        for (uint32_t i = 0; i < raw.size() / 32; i++) {
            uint8_t* e = &raw[i * 32];
            if (e[0] == 0) break;
            if (e[0] != 0xE5 && e[11] != FAT_LFN && (e[11] & FAT_VOLID) && !(e[11] & FAT_DIR)) {
                memcpy(e, l, 11);
                write_dir_entries(cl, i, e, 1);
                memcpy(label, l, 11);
                label[11] = 0;
                for (int k = 10; k >= 0 && label[k] == ' '; k--) label[k] = 0;
                sync();
                return true;
            }
        }
    }
    uint8_t e[32] = {};
    memcpy(e, l, 11);
    e[11] = FAT_VOLID;
    e[12] = 0x80;
    FatEntry out;
    bool ok = insert_entry(root_cluster, "", e, out);
    memcpy(label, l, 11);
    label[11] = 0;
    for (int k = 10; k >= 0 && label[k] == ' '; k--) label[k] = 0;
    sync();
    return ok;
}

// ---------------------------------------------------------------------------
// Formateo
// ---------------------------------------------------------------------------
bool Fat32::format(BlockDevice* dev, const char* lbl) {
    uint32_t total = (uint32_t)min<uint64_t>(dev->sectors, 0xFFFFFFFFull);
    if (total < 66600) return false; // FAT32 necesita al menos 65525 clústeres
    uint32_t spc;
    uint64_t mb = (uint64_t)total / 2048;
    if (mb <= 260) spc = 1;
    else if (mb <= 8192) spc = 8;
    else if (mb <= 16384) spc = 16;
    else if (mb <= 32768) spc = 32;
    else spc = 64;
    uint32_t reserved = 32, nfats = 2;
    uint32_t fatsz = 0, clusters = 0;
    for (;;) {
        uint32_t tmp1 = total - reserved;
        uint32_t tmp2 = (256 * spc + nfats) / 2;
        fatsz = (tmp1 + tmp2 - 1) / tmp2;
        clusters = (total - reserved - nfats * fatsz) / spc;
        if (clusters >= 65525 || spc == 1) break;
        spc /= 2;
    }
    if (clusters < 65525) return false;
    klog("FAT32: formateando %s: %u sectores, %u por clúster, FAT de %u sectores\n", dev->name, total, spc, fatsz);

    uint8_t bs[512] = {};
    bs[0] = 0xEB; bs[1] = 0x58; bs[2] = 0x90;
    memcpy(bs + 3, "BRYSOS  ", 8);
    wr16(bs + 11, 512);
    bs[13] = (uint8_t)spc;
    wr16(bs + 14, (uint16_t)reserved);
    bs[16] = (uint8_t)nfats;
    bs[21] = 0xF8;
    wr16(bs + 24, 63);
    wr16(bs + 26, 255);
    wr32(bs + 28, (uint32_t)dev->offset);
    wr32(bs + 32, total);
    wr32(bs + 36, fatsz);
    wr32(bs + 44, 2);
    wr16(bs + 48, 1);
    wr16(bs + 50, 6);
    bs[64] = 0x80;
    bs[66] = 0x29;
    wr32(bs + 67, (uint32_t)(pit::ticks() * 2654435761u) ^ 0x42525953);
    char l[11];
    memset(l, ' ', 11);
    for (int i = 0; i < 11 && lbl[i]; i++) l[i] = to_upper(lbl[i]);
    memcpy(bs + 71, l, 11);
    memcpy(bs + 82, "FAT32   ", 8);
    // Código de arranque mínimo: muestra un aviso y espera
    static const uint8_t code[] = {0x0E, 0x1F, 0xBE, 0x77, 0x7C, 0xAC, 0x22, 0xC0, 0x74, 0x0B, 0x56, 0xB4,
                                   0x0E, 0xBB, 0x07, 0x00, 0xCD, 0x10, 0x5E, 0xEB, 0xF0, 0x32, 0xE4, 0xCD,
                                   0x16, 0xCD, 0x19, 0xEB, 0xFE};
    memcpy(bs + 90, code, sizeof code);
    const char* msg = "Este disco de datos de BrysOS no es de arranque.\r\nPulse una tecla para reiniciar...\r\n";
    memcpy(bs + 119, msg, strlen(msg));
    bs[510] = 0x55; bs[511] = 0xAA;

    uint8_t fi[512] = {};
    wr32(fi, 0x41615252);
    wr32(fi + 484, 0x61417272);
    wr32(fi + 488, clusters - 1);
    wr32(fi + 492, 3);
    wr32(fi + 508, 0xAA550000);

    // Sectores reservados a cero, después sector de arranque, FSInfo y copias
    uint8_t* zero = (uint8_t*)mm::kcalloc(64 * 512);
    for (uint32_t s = 0; s < reserved; s += 32)
        if (!dev->write(s, min(32u, reserved - s), zero)) { mm::kfree(zero); return false; }
    dev->write(0, 1, bs);
    dev->write(1, 1, fi);
    dev->write(6, 1, bs);
    dev->write(7, 1, fi);
    // FAT vacías
    for (uint32_t k = 0; k < nfats; k++) {
        uint32_t base = reserved + k * fatsz;
        for (uint32_t s = 0; s < fatsz; s += 64)
            if (!dev->write(base + s, min(64u, fatsz - s), zero)) { mm::kfree(zero); return false; }
        uint8_t first[512] = {};
        wr32(first, 0x0FFFFFF8);
        wr32(first + 4, 0x0FFFFFFF);
        wr32(first + 8, 0x0FFFFFFF); // clúster raíz
        dev->write(base, 1, first);
    }
    // Directorio raíz con la etiqueta del volumen
    uint32_t data = reserved + nfats * fatsz;
    for (uint32_t s = 0; s < spc; s += 64) dev->write(data + s, min(64u, spc - s), zero);
    uint8_t root[512] = {};
    memcpy(root, l, 11);
    root[11] = FAT_VOLID;
    DateTime now = rtc::now();
    wr16(root + 22, rtc::fat_time(now));
    wr16(root + 24, rtc::fat_date(now));
    dev->write(data, 1, root);
    mm::kfree(zero);
    dev->flush();
    return true;
}

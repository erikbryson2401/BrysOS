// Capa de sistema de archivos virtual: unidades con letra sobre volúmenes FAT32
#include "fs/vfs.h"
#include "fs/fat32.h"
#include "drivers/block.h"
#include "lib/log.h"
#include "lib/string.h"
#include "arch/idt.h"
#include "boot/bootinfo.h"
#include "kernel.h"
#include "lib/i18n.h"
#include "sys/paths.h"
#include "fs/partition.h"
#include "lib/notice.h"

static void explain_if_unusable(BlockDevice* d, bool at_boot);

namespace {
Vec<Volume*> vols;
Vec<BlockDevice*> partitions;
const char* err = "";
bool on_ram = false;
bool mounted = false;
uint32_t gen = 1;

void set_err(const char* e) { err = e; }

bool all_zero(const uint8_t* p, int n) {
    for (int i = 0; i < n; i++) if (p[i]) return false;
    return true;
}

void scan_disk(BlockDevice* d) {
    uint8_t mbr[512];
    if (!d->read(0, 1, mbr)) { klog("%s: no se puede leer el sector 0\n", d->name); return; }
    if (all_zero(mbr, 512)) { klog("%s: disco vacío (sin particiones)\n", d->name); return; }
    if (Fat32::looks_like_fat32(mbr)) {
        // FAT32 sin tabla de particiones
        auto* p = new Partition(d, 0, d->sectors, 0);
        partitions.push(p);
        return;
    }
    if (mbr[510] != 0x55 || mbr[511] != 0xAA) return;
    bool gpt = false;
    for (int i = 0; i < 4; i++) {
        uint8_t* e = mbr + 446 + i * 16;
        uint8_t type = e[4];
        uint32_t start = e[8] | (e[9] << 8) | (e[10] << 16) | ((uint32_t)e[11] << 24);
        uint32_t count = e[12] | (e[13] << 8) | (e[14] << 16) | ((uint32_t)e[15] << 24);
        if (type == 0xEE) { gpt = true; break; }
        if (!type || !count || start + (uint64_t)count > d->sectors + 1) continue;
        auto* p = new Partition(d, start, count, i + 1);
        p->type = type;
        partitions.push(p);
    }
    if (!gpt) return;
    uint8_t hdr[512];
    if (!d->read(1, 1, hdr) || memcmp(hdr, "EFI PART", 8)) return;
    uint64_t ent_lba = *(uint64_t*)(hdr + 72);
    uint32_t ent_count = *(uint32_t*)(hdr + 80);
    uint32_t ent_size = *(uint32_t*)(hdr + 84);
    if (ent_size < 128 || ent_size > 512 || ent_count > 256) return;
    uint32_t bytes = ent_count * ent_size;
    auto* buf = (uint8_t*)mm::kmalloc((bytes + 511) & ~511u);
    if (d->read(ent_lba, (bytes + 511) / 512, buf)) {
        for (uint32_t i = 0; i < ent_count; i++) {
            uint8_t* e = buf + i * ent_size;
            if (all_zero(e, 16)) continue;
            uint64_t first = *(uint64_t*)(e + 32), last = *(uint64_t*)(e + 40);
            if (last < first || last >= d->sectors) continue;
            auto* p = new Partition(d, first, last - first + 1, (int)i + 1);
            p->type = 0xEE;
            partitions.push(p);
        }
    }
    mm::kfree(buf);
}

char free_letter() {
    for (char l = 'D'; l < 'S'; l++) if (!vfs::volume(l)) return l;
    return 0;
}

// Monta (en lectura y escritura) las particiones FAT32 de un disco recién conectado
void mount_extra(size_t first_partition) {
    for (size_t i = first_partition; i < partitions.size(); i++) {
        BlockDevice* p = partitions[i];
        Fat32* fs = Fat32::mount(p, false);
        if (!fs) continue;
        char l = free_letter();
        if (!l) { delete fs; break; }
        auto* v = new Volume();
        v->letter = l;
        v->fs = fs;
        v->dev = p;
        v->removable = true;
        v->label = fs->label;
        vols.push(v);
        klog("Unidad %c: FAT32 \"%s\" en %s, %lu MiB (extraíble)\n", l, fs->label, p->name, fs->total_bytes() >> 20);
    }
    vols.sort([](Volume* a, Volume* b) { return (int)a->letter - (int)b->letter; });
}

void mount_all() {
    // libera los volúmenes anteriores
    for (Volume* v : vols) {
        if (v->ram) { mm::kfree(((RamDisk*)v->dev)->data); delete v->dev; }
        delete v->fs;
        delete v;
    }
    for (BlockDevice* p : partitions) delete p;
    vols.clear();
    partitions.clear();
    // los discos internos reciben las primeras letras; los USB, las siguientes
    for (BlockDevice* d : block::disks()) if (!d->removable) scan_disk(d);
    for (BlockDevice* d : block::disks()) if (d->removable) scan_disk(d);
    // Primero el volumen BRYSOS (C:), luego el resto en sólo lectura
    char next = 'D';
    bool have_c = false;
    // Partición del sistema: la indicada por GRUB (root=XXXX-XXXX, número de serie FAT32)
    // o, si no se indica, la que tenga la etiqueta BRYSOS. En modo live (pendrive) se usa la RAM.
    char root[32] = {};
    bool has_root = cmdline_get("root", root, sizeof root);
    bool live = live_mode();
    for (BlockDevice* p : partitions) {
        Fat32* fs = Fat32::mount(p, false);
        if (!fs) continue;
        char serial[16];
        ksnprintf(serial, sizeof serial, "%04X-%04X", fs->serial >> 16, fs->serial & 0xFFFF);
        bool sys = !live && !p->parent->removable && (has_root ? !stricmp(serial, root) : !stricmp(fs->label, "BRYSOS"));
        auto* v = new Volume();
        v->fs = fs;
        v->dev = p;
        v->label = fs->label;
        v->removable = p->parent->removable;
        if (sys && !have_c) {
            v->letter = 'C';
            have_c = true;
        } else {
            // las demás particiones internas, en sólo lectura; los pendrives se pueden escribir
            if (!v->removable) {
                fs->read_only = true;
                v->read_only = true;
            }
            v->letter = next++;
        }
        klog("Unidad %c: %s \"%s\" en %s, %lu MiB%s\n", v->letter, "FAT32", fs->label, p->name,
             fs->total_bytes() >> 20, v->read_only ? " (sólo lectura)" : "");
        vols.push(v);
    }
    on_ram = !have_c;
    if (!have_c) {
        // Sin disco BRYSOS: unidad C: en memoria (los cambios se pierden al apagar)
        auto* rd = new RamDisk(48 * 2048);
        Fat32::format(rd, "BRYSOS");
        Fat32* fs = Fat32::mount(rd, false);
        auto* v = new Volume();
        v->letter = 'C';
        v->fs = fs;
        v->dev = rd;
        v->ram = true;
        v->label = "BRYSOS";
        vols.push(v);
        klog("Unidad C: en memoria (no se encontró ninguna partición BRYSOS)\n");
    }
    vols.sort([](Volume* a, Volume* b) { return (int)a->letter - (int)b->letter; });
    mounted = true;
    for (BlockDevice* d : block::disks())
        if (d->removable) explain_if_unusable(d, true);
    gen++;
}

// Separa "C:\a\b" en volumen y ruta interna
Volume* split(const char* path, const char*& rest) {
    if (!path[0] || path[1] != ':') { set_err(T("Ruta no válida")); return nullptr; }
    Volume* v = vfs::volume(path[0]);
    if (!v) { set_err(T("La unidad no existe")); return nullptr; }
    rest = path + 2;
    return v;
}

bool resolve(const char* path, Volume*& v, FatEntry& e) {
    String n = vfs::normalize(path);
    const char* rest;
    v = split(n.c_str(), rest);
    if (!v) return false;
    if (!v->fs->resolve(rest, e)) { set_err(T("No se encuentra el archivo o la carpeta")); return false; }
    return true;
}

void to_vfs(const FatEntry& f, VfsEntry& out) {
    out.name = f.name;
    out.dir = f.is_dir();
    out.size = f.size;
    out.attr = f.attr;
    out.mtime = rtc::from_fat(f.mdate, f.mtime);
    out.ctime = rtc::from_fat(f.cdate, f.ctime);
}

void after_write(Volume* v) {
    if (!v || !v->removable) return;
    v->fs->sync();
    v->dev->flush();
}

bool writable(Volume* v) {
    if (v->read_only) { set_err(T("La unidad es de sólo lectura")); return false; }
    return true;
}
}

void vfs::init() { mount_all(); }

char vfs::mount_rw(BlockDevice* dev) {
    Fat32* fs = Fat32::mount(dev, false);
    if (!fs) return 0;
    char letter = 'S';
    while (letter <= 'Z' && volume(letter)) letter++;
    if (letter > 'Z') { delete fs; return 0; }
    auto* v = new Volume();
    v->letter = letter;
    v->fs = fs;
    v->dev = dev;
    v->label = fs->label;
    v->temporary = true;
    vols.push(v);
    return letter;
}

void vfs::unmount(char letter) {
    for (size_t i = 0; i < vols.size(); i++) {
        Volume* v = vols[i];
        if (v->letter != to_upper(letter)) continue;
        v->fs->sync();
        vols.erase(i);
        delete v->fs;
        delete v->dev;
        delete v;
        return;
    }
}
void vfs::remount() { mount_all(); }

void vfs::disk_added(BlockDevice* d) {
    if (!mounted) return;   // durante el arranque lo monta vfs::init()
    size_t first = partitions.size();
    scan_disk(d);
    mount_extra(first);
    gen++;
    explain_if_unusable(d, false);
}

// Avisa si un disco extraíble no ha dado ninguna unidad
static void explain_if_unusable(BlockDevice* d, bool at_boot) {
    for (Volume* v : vols)
        if (v->dev == d || v->dev->parent == d) return;
    // Ninguna partición utilizable: se explica por qué
    String why;
    uint8_t s[512];
    if (d->read(64, 1, s) && !memcmp(s + 1, "CD001", 5)) {
        // al arrancar suele ser el propio pendrive de instalación: no se avisa
        if (at_boot) return;
        why = T("Contiene una imagen de CD/DVD (ISO) grabada en modo DD, como el pendrive de instalación.\n"
                "Copie los archivos a un pendrive con formato FAT32.");
    } else {
        DiskLayout l;
        String found;
        if (part::read_layout(d, l))
            for (auto& p : l.parts) {
                if (!found.empty()) found += ", ";
                found += T(part::fs_name(p.fs));
            }
        if (found.empty()) why = T("No tiene ninguna partición con un formato reconocible.");
        else why = String::format(T("No tiene ninguna partición FAT32 (formato encontrado: %s).\n"
                                    "BrysOS sólo puede leer pendrives con formato FAT32."), found.c_str());
    }
    klog("%s: sin particiones FAT32 que montar\n", d->name);
    notice::post(T("Disco extraíble"), String::format(T("Se ha conectado \"%s\", pero no se puede usar:\n\n%s"), d->model, why.c_str()).c_str());
}

void vfs::disk_removed(BlockDevice* d) {
    if (!mounted) return;
    for (size_t i = vols.size(); i > 0; i--) {
        Volume* v = vols[i - 1];
        if (v->dev != d && v->dev->parent != d) continue;
        klog("Unidad %c: retirada\n", v->letter);
        vols.erase(i - 1);
        delete v->fs;
        partitions.remove(v->dev);
        delete v->dev;
        delete v;
    }
    // particiones sin volumen (no FAT32) del mismo disco
    for (size_t i = partitions.size(); i > 0; i--) {
        BlockDevice* p = partitions[i - 1];
        if (p->parent == d) { partitions.erase(i - 1); delete p; }
    }
    gen++;
}

uint32_t vfs::generation() { return gen; }

bool vfs::eject(char letter, String& model) {
    Volume* v = volume(letter);
    if (!v || !v->removable) { set_err(T("Esta unidad no se puede expulsar.")); return false; }
    BlockDevice* disk = v->dev->parent ? v->dev->parent : v->dev;
    model = disk->model;
    // todo lo pendiente al dispositivo y fuera las unidades de ese disco
    for (Volume* o : vols)
        if (o->dev == disk || o->dev->parent == disk) o->fs->sync();
    disk->flush();
    for (size_t i = vols.size(); i > 0; i--) {
        Volume* o = vols[i - 1];
        if (o->dev != disk && o->dev->parent != disk) continue;
        klog("Unidad %c: expulsada\n", o->letter);
        vols.erase(i - 1);
        delete o->fs;
        partitions.remove(o->dev);
        delete o->dev;
        delete o;
    }
    for (size_t i = partitions.size(); i > 0; i--) {
        BlockDevice* p = partitions[i - 1];
        if (p->parent == disk) { partitions.erase(i - 1); delete p; }
    }
    gen++;
    return true;
}
Vec<Volume*>& vfs::volumes() { return vols; }
bool vfs::system_on_ram() { return on_ram; }
const char* vfs::error() { return err; }

Volume* vfs::volume(char letter) {
    letter = to_upper(letter);
    for (Volume* v : vols) if (v->letter == letter) return v;
    return nullptr;
}

Vec<BlockDevice*> vfs::blank_disks() {
    Vec<BlockDevice*> out;
    uint8_t s[512];
    for (BlockDevice* d : block::disks())
        if (!d->removable && d->read(0, 1, s) && all_zero(s, 512) && d->sectors >= 70000) out.push(d);
    return out;
}

bool vfs::format_disk(BlockDevice* d) {
    uint8_t mbr[512] = {};
    uint64_t start = 2048;
    uint64_t count = d->sectors - start;
    if (count > 0xFFFFFFFFull) count = 0xFFFFFFFFull;
    uint8_t* e = mbr + 446;
    e[0] = 0x80;
    e[1] = 0xFE; e[2] = 0xFF; e[3] = 0xFF;   // CHS no utilizado (LBA)
    e[4] = 0x0C;                              // FAT32 LBA
    e[5] = 0xFE; e[6] = 0xFF; e[7] = 0xFF;
    for (int i = 0; i < 4; i++) {
        e[8 + i] = (uint8_t)(start >> (8 * i));
        e[12 + i] = (uint8_t)(count >> (8 * i));
    }
    uint32_t sig = (uint32_t)(pit::ticks() * 2246822519u) ^ 0x42727973;
    memcpy(mbr + 440, &sig, 4);
    mbr[510] = 0x55;
    mbr[511] = 0xAA;
    if (!d->write(0, 1, mbr)) { set_err(T("No se pudo escribir la tabla de particiones")); return false; }
    Partition p(d, start, count, 1);
    if (!Fat32::format(&p, "BRYSOS")) { set_err(T("No se pudo dar formato a la partición")); return false; }
    return true;
}

namespace {
// Devuelve la carpeta con el nombre del idioma actual, o la del otro idioma si sólo existe ésa
String special(const char* es, const char* en) {
    String a = String("C:\\") + T(es);
    if (vfs::exists(a.c_str())) return a;
    String b = String("C:\\") + es, c = String("C:\\") + en;
    if (vfs::exists(b.c_str())) return b;
    if (vfs::exists(c.c_str())) return c;
    return a;
}
}

String vfs::docs_dir() { return special("Mis documentos", "My Documents"); }
String vfs::trash_dir() { return special("Papelera", "Recycled"); }
String vfs::pictures_dir() {
    String d = docs_dir();
    String a = join(d.c_str(), T("Mis imágenes"));
    if (exists(a.c_str())) return a;
    String b = join(d.c_str(), "Mis imágenes"), c = join(d.c_str(), "My Pictures");
    if (exists(b.c_str())) return b;
    if (exists(c.c_str())) return c;
    return a;
}

void vfs::ensure_defaults() {
    mkdir(docs_dir().c_str());
    mkdir(pictures_dir().c_str());
    mkdir(trash_dir().c_str());
    {
        SystemWrite sw;
        mkdir(sys::DIR);
    }
    String readme = join(docs_dir().c_str(), T("Léame.txt"));
    if (!exists(readme.c_str())) {
        const char* txt = T(
            "Bienvenido a BrysOS\r\n"
            "===================\r\n\r\n"
            "Este archivo está guardado de verdad en el disco, en una partición FAT32.\r\n"
            "Puede abrirlo, modificarlo y guardarlo con el Bloc de notas; los cambios\r\n"
            "se conservan al reiniciar el equipo.\r\n\r\n"
            "Aplicaciones incluidas:\r\n"
            "  - Explorador (Mi PC)\r\n"
            "  - Bloc de notas\r\n"
            "  - Paint (guarda imágenes BMP)\r\n"
            "  - Calculadora\r\n"
            "  - Símbolo del sistema (escriba AYUDA para ver las órdenes)\r\n"
            "  - Buscar archivos o carpetas\r\n"
            "  - Buscaminas\r\n");
        write_file(readme.c_str(), txt, (uint32_t)strlen(txt));
    }
}

bool vfs::list(const char* path, Vec<VfsEntry>& out) {
    out.clear();
    Volume* v;
    FatEntry e;
    if (!resolve(path, v, e)) return false;
    if (!e.is_dir()) { set_err(T("No es una carpeta")); return false; }
    Vec<FatEntry> ents;
    if (!v->fs->list(e, ents)) { set_err(T("Error al leer la carpeta")); return false; }
    for (auto& f : ents) {
        VfsEntry ve;
        to_vfs(f, ve);
        out.push(move_val(ve));
    }
    return true;
}

bool vfs::stat(const char* path, VfsEntry& out) {
    Volume* v;
    FatEntry e;
    if (!resolve(path, v, e)) return false;
    to_vfs(e, out);
    if (e.is_root) { out.name = String(path).substr(0, 2); out.dir = true; }
    return true;
}

bool vfs::exists(const char* path) {
    Volume* v;
    FatEntry e;
    return resolve(path, v, e);
}

bool vfs::is_dir(const char* path) {
    Volume* v;
    FatEntry e;
    return resolve(path, v, e) && e.is_dir();
}

bool vfs::read_file(const char* path, uint8_t*& data, uint32_t& size) {
    data = nullptr;
    size = 0;
    Volume* v;
    FatEntry e;
    if (!resolve(path, v, e)) return false;
    if (e.is_dir()) { set_err(T("Es una carpeta")); return false; }
    data = (uint8_t*)mm::kmalloc(e.size + 1);
    if (!data) { set_err(T("Memoria insuficiente")); return false; }
    if (!v->fs->read(e, data, 0, e.size)) {
        mm::kfree(data);
        data = nullptr;
        set_err(T("Error de lectura del disco"));
        return false;
    }
    data[e.size] = 0;
    size = e.size;
    return true;
}

// ---------------------------------------------------------------------------
// Carpeta del sistema protegida
// ---------------------------------------------------------------------------
namespace {
int system_scope = 0;

bool allowed(const String& n) {
    if (system_scope > 0 || !vfs::is_system_path(n.c_str())) return true;
    set_err(T("Acceso denegado: la carpeta C:\\BrysOS contiene los archivos del sistema y está protegida.\n"
              "Para actualizar BrysOS, abra un paquete de actualización (.bsi)."));
    return false;
}
}

vfs::SystemWrite::SystemWrite() { system_scope++; }
vfs::SystemWrite::~SystemWrite() { system_scope--; }

bool vfs::is_system_path(const char* path) {
    String n = normalize(path);
    size_t len = strlen(sys::DIR);
    if (n.size() < len) return false;
    for (size_t i = 0; i < len; i++)
        if (to_upper(n[i]) != to_upper(sys::DIR[i])) return false;
    return n.size() == len || n[len] == '\\';
}

bool vfs::write_file(const char* path, const void* data, uint32_t size) {
    String n = normalize(path);
    const char* rest;
    Volume* v = split(n.c_str(), rest);
    if (!v || !writable(v) || !allowed(n)) return false;
    FatEntry e;
    bool found = v->fs->resolve(rest, e);
    if (found) {
        if (e.is_dir()) { set_err(T("Ya existe una carpeta con ese nombre")); return false; }
        if (e.attr & FAT_RO) { set_err(T("El archivo es de sólo lectura")); return false; }
    } else {
        FatEntry dir;
        String par = parent(n.c_str());
        if (!v->fs->resolve(par.c_str() + 2, dir) || !dir.is_dir()) { set_err(T("La carpeta de destino no existe")); return false; }
        if (!v->fs->create(dir, basename(n.c_str()).c_str(), FAT_ARCHIVE, e)) {
            set_err(T("No se pudo crear el archivo (nombre no válido o disco lleno)"));
            return false;
        }
    }
    if (!v->fs->write_all(e, data, size)) { set_err(T("Error de escritura (¿disco lleno?)")); after_write(v); return false; }
    after_write(v);
    return true;
}

bool vfs::mkdir(const char* path) {
    String n = normalize(path);
    const char* rest;
    Volume* v = split(n.c_str(), rest);
    if (!v || !writable(v) || !allowed(n)) return false;
    FatEntry e;
    if (v->fs->resolve(rest, e)) { set_err(T("Ya existe un archivo o carpeta con ese nombre")); return false; }
    FatEntry dir;
    String par = parent(n.c_str());
    if (!v->fs->resolve(par.c_str() + 2, dir) || !dir.is_dir()) { set_err(T("La carpeta superior no existe")); return false; }
    if (!v->fs->create(dir, basename(n.c_str()).c_str(), FAT_DIR, e)) { set_err(T("No se pudo crear la carpeta")); return false; }
    after_write(v);
    return true;
}

bool vfs::remove(const char* path) {
    Volume* v;
    FatEntry e;
    if (!resolve(path, v, e) || !writable(v) || !allowed(normalize(path))) return false;
    if (e.is_root) { set_err(T("No se puede eliminar la raíz de una unidad")); return false; }
    if (!v->fs->remove(e)) { set_err(e.is_dir() ? T("La carpeta no está vacía") : T("No se pudo eliminar")); return false; }
    after_write(v);
    return true;
}

bool vfs::remove_tree(const char* path) {
    String n = normalize(path);
    if (is_dir(n.c_str())) {
        Vec<VfsEntry> ents;
        if (!list(n.c_str(), ents)) return false;
        for (auto& e : ents)
            if (!remove_tree(join(n.c_str(), e.name.c_str()).c_str())) return false;
    }
    return remove(n.c_str());
}

bool vfs::rename(const char* from, const char* to) {
    String a = normalize(from), b = normalize(to);
    if (!allowed(a) || !allowed(b)) return false;
    if (to_upper(a[0]) != to_upper(b[0])) {
        // entre unidades distintas: copiar y borrar
        if (!copy(a.c_str(), b.c_str())) return false;
        return remove_tree(a.c_str());
    }
    // no se puede mover una carpeta dentro de sí misma
    if (b.size() > a.size() && !strncmp(String(b).c_str(), a.c_str(), a.size()) && b[a.size()] == '\\') {
        bool same = true;
        for (size_t i = 0; i < a.size(); i++) if (to_upper(a[i]) != to_upper(b[i])) same = false;
        if (same) { set_err(T("No se puede mover una carpeta dentro de sí misma")); return false; }
    }
    Volume* v;
    FatEntry e;
    if (!resolve(a.c_str(), v, e) || !writable(v)) return false;
    String par = parent(b.c_str());
    FatEntry dir;
    if (!v->fs->resolve(par.c_str() + 2, dir) || !dir.is_dir()) { set_err(T("La carpeta de destino no existe")); return false; }
    if (!v->fs->rename(e, dir, basename(b.c_str()).c_str())) {
        set_err(T("No se pudo cambiar el nombre (¿ya existe o el nombre no es válido?)"));
        return false;
    }
    after_write(v);
    return true;
}

bool vfs::copy(const char* from, const char* to) {
    String a = normalize(from), b = normalize(to);
    if (is_dir(a.c_str())) {
        if (!mkdir(b.c_str())) return false;
        Vec<VfsEntry> ents;
        if (!list(a.c_str(), ents)) return false;
        for (auto& e : ents)
            if (!copy(join(a.c_str(), e.name.c_str()).c_str(), join(b.c_str(), e.name.c_str()).c_str())) return false;
        return true;
    }
    if (exists(b.c_str())) { set_err(T("El destino ya existe")); return false; }
    uint8_t* data;
    uint32_t size;
    uint64_t t0 = pit::ticks();
    if (!read_file(a.c_str(), data, size)) return false;
    uint64_t t1 = pit::ticks();
    bool ok = write_file(b.c_str(), data, size);
    uint64_t t2 = pit::ticks();
    mm::kfree(data);
    // velocidad de las copias grandes en el registro (para comprobar los pendrives)
    if (ok && size >= (1u << 20)) {
        auto rate = [&](uint64_t ms) { return ms ? (uint64_t)size * 1000 / ms / 1024 : 0; };
        klog("Copia: %s -> %s, %u KB: lectura %lu ms (%lu KB/s), escritura %lu ms (%lu KB/s)\n", a.c_str(), b.c_str(),
             size >> 10, t1 - t0, rate(t1 - t0), t2 - t1, rate(t2 - t1));
    }
    return ok;
}

bool vfs::set_attr(const char* path, uint8_t attr) {
    Volume* v;
    FatEntry e;
    if (!resolve(path, v, e) || !writable(v) || !allowed(normalize(path))) return false;
    bool ok = v->fs->set_attr(e, attr);
    after_write(v);
    return ok;
}

// ---------------------------------------------------------------------------
// Rutas
// ---------------------------------------------------------------------------
String vfs::normalize(const char* path, const char* cwd) {
    String in;
    if (path[0] && path[1] == ':') {
        in = path;
    } else if (cwd && cwd[0]) {
        if (path[0] == '\\' || path[0] == '/') { in = String(cwd).substr(0, 2); in += path; }
        else { in = cwd; in += "\\"; in += path; }
    } else {
        in = "C:\\";
        in += path;
    }
    // Componentes, resolviendo "." y ".."
    Vec<String> comps;
    const char* p = in.c_str() + 2;
    while (*p) {
        while (*p == '\\' || *p == '/') p++;
        if (!*p) break;
        const char* s = p;
        while (*p && *p != '\\' && *p != '/') p++;
        String c(s, (size_t)(p - s));
        // quita espacios y puntos finales
        while (c.size() && (c[c.size() - 1] == ' ' || (c[c.size() - 1] == '.' && c != "." && c != ".."))) c.erase(c.size() - 1, 1);
        if (c.empty() || c == ".") continue;
        if (c == "..") { if (comps.size()) comps.pop(); continue; }
        comps.push(move_val(c));
    }
    String out;
    out.append(to_upper(in[0]));
    out += ":";
    if (comps.empty()) { out += "\\"; return out; }
    for (auto& c : comps) { out += "\\"; out += c; }
    return out;
}

String vfs::parent(const char* path) {
    String p = normalize(path);
    int i = p.rfind('\\');
    if (i <= 2) return p.substr(0, 3);
    return p.substr(0, (size_t)i);
}

String vfs::basename(const char* path) {
    String p = normalize(path);
    int i = p.rfind('\\');
    if (p.size() <= 3) return p;
    return p.substr((size_t)i + 1);
}

String vfs::join(const char* dir, const char* name) {
    String d = dir;
    if (d.size() && d[d.size() - 1] != '\\') d += "\\";
    d += name;
    return d;
}

String vfs::extension(const char* path) {
    String b = basename(path);
    int i = b.rfind('.');
    if (i <= 0) return String();
    String e = b.substr((size_t)i + 1);
    for (size_t k = 0; k < e.size(); k++) e[k] = to_upper(e[k]);
    return e;
}

String vfs::unique_name(const char* dir, const char* base, const char* ext) {
    String n = join(dir, (String(base) + ext).c_str());
    for (int i = 2; exists(n.c_str()) && i < 1000; i++)
        n = join(dir, String::format("%s (%d)%s", base, i, ext).c_str());
    return n;
}

String vfs::format_size(uint64_t b) {
    if (b < 1024) return String::format("%lu bytes", b);
    if (b < 1024 * 1024) return String::format("%lu KB", (b + 1023) / 1024);
    if (b < 1024ull * 1024 * 1024) return String::format("%lu,%lu MB", b >> 20, ((b & 0xFFFFF) * 10) >> 20);
    return String::format("%lu,%lu GB", b >> 30, ((b & 0x3FFFFFFF) * 10) >> 30);
}

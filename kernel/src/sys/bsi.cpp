// Paquetes de actualización .bsi: lectura, comprobación y aplicación sobre C:\BrysOS
#include "sys/bsi.h"
#include "sys/paths.h"
#include "fs/vfs.h"
#include "fs/fat32.h"
#include "fs/partition.h"
#include "fs/bootloader.h"
#include "drivers/block.h"
#include "drivers/rtc.h"
#include "lib/log.h"
#include "lib/string.h"
#include "lib/i18n.h"
#include "kernel.h"
#include "apps/settings.h"

namespace {
constexpr char MAGIC[8] = {'B', 'R', 'Y', 'S', 'B', 'S', 'I', 0x1A};
constexpr uint32_t FORMAT = 1;
constexpr uint32_t MAX_PACKAGE = 256u << 20;
constexpr const char* STAGE = "C:\\BrysOS\\_actualizacion";

// Archivos que gestiona el propio sistema: un paquete no puede escribirlos ni borrarlos
const char* const RESERVED[] = {"grub.cfg", "config.ini", "version.txt", "update.pending", "kernel.old",
                                "mbr-original.bin", "actualizaciones.log", "_actualizacion", "fondo.bmp", nullptr};

uint32_t rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

bool ieq(const String& a, const char* b) { return !stricmp(a.c_str(), b); }

// Comprueba una ruta relativa a C:\BrysOS y la deja con '\' como separador
bool clean_path(const String& in, String& out, bool for_delete, String& err) {
    out.clear();
    String comp;
    Vec<String> comps;
    for (size_t i = 0; i <= in.size(); i++) {
        char c = i < in.size() ? in[i] : '\\';
        if (c == '/' || c == '\\') {
            if (comp.empty() || comp == "." || comp == ".." || comp[comp.size() - 1] == ' ' || comp[comp.size() - 1] == '.') {
                err = String::format(T("Ruta no permitida en el paquete: \"%s\""), in.c_str());
                return false;
            }
            comps.push(move_val(comp));
            comp = String();
            continue;
        }
        if ((uint8_t)c < 32 || strchr("<>:\"|?*", c)) {
            err = String::format(T("Ruta no permitida en el paquete: \"%s\""), in.c_str());
            return false;
        }
        comp.append(c);
    }
    if (comps.empty() || in.size() > 200) {
        err = String::format(T("Ruta no permitida en el paquete: \"%s\""), in.c_str());
        return false;
    }
    for (int i = 0; RESERVED[i]; i++)
        if (ieq(comps[0], RESERVED[i])) {
            err = String::format(T("El paquete intenta modificar \"%s\", que gestiona el propio sistema."), comps[0].c_str());
            return false;
        }
    if (for_delete && comps.size() == 1 && ieq(comps[0], "kernel.elf")) {
        err = T("El paquete intenta borrar el núcleo del sistema.");
        return false;
    }
    for (size_t i = 0; i < comps.size(); i++) {
        if (i) out += "\\";
        out += comps[i];
    }
    return true;
}

String full_path(const String& rel) { return vfs::join(sys::DIR, rel.c_str()); }
String staged_path(size_t i) { return String::format("%s\\F%u.TMP", STAGE, (unsigned)i); }

// Crea las carpetas que falten entre C:\BrysOS y 'dir'
bool make_dirs(const String& dir) {
    if (vfs::is_dir(dir.c_str())) return true;
    String par = vfs::parent(dir.c_str());
    if (par.size() < strlen(sys::DIR)) return false;
    if (!make_dirs(par)) return false;
    return vfs::mkdir(dir.c_str());
}

bool write_verified(const char* path, const void* data, uint32_t size) {
    if (!vfs::write_file(path, data, size)) return false;
    uint8_t* back;
    uint32_t n;
    if (!vfs::read_file(path, back, n)) return false;
    bool ok = n == size && part::crc32(back, n) == part::crc32(data, size);
    mm::kfree(back);
    return ok;
}

String to_utf8(const String& s) {
    String out;
    for (size_t i = 0; i < s.size(); i++) {
        uint8_t c = (uint8_t)s[i];
        if (c < 0x80) out.append((char)c);
        else { out.append((char)(0xC0 | (c >> 6))); out.append((char)(0x80 | (c & 0x3F))); }
    }
    return out;
}

String trim(const char* s) {
    while (*s == ' ' || *s == '\t') s++;
    String r = s;
    while (r.size() && (r[r.size() - 1] == ' ' || r[r.size() - 1] == '\t' || r[r.size() - 1] == '\r')) r.erase(r.size() - 1, 1);
    return r;
}

Vec<String> split_lines(const char* text, size_t len) {
    Vec<String> lines;
    size_t start = 0;
    for (size_t i = 0; i <= len; i++) {
        if (i == len || text[i] == '\n') {
            String l(text + start, i - start);
            if (l.size() && l[l.size() - 1] == '\r') l.erase(l.size() - 1, 1);
            lines.push(move_val(l));
            start = i + 1;
        }
    }
    return lines;
}

// Añade a grub.cfg una entrada que arranca kernel.old y la deja como alternativa automática
// (fallback) por si el núcleo nuevo no llega a cargarse.
bool add_grub_fallback() {
    uint8_t* data;
    uint32_t size;
    if (!vfs::read_file(sys::GRUB_CFG, data, size)) return false;
    Vec<String> lines = split_lines((const char*)data, size);
    mm::kfree(data);
    for (auto& l : lines)
        if (strstr(l.c_str(), sys::GRUB_KERNEL_OLD)) return true;   // ya la tiene
    // bloque menuentry que arranca /brysos/kernel.elf
    int start = -1, end = -1, entries_before = 0, seen_entries = 0;
    for (size_t i = 0; i < lines.size() && end < 0; i++) {
        String t = trim(lines[i].c_str());
        if (!strncmp(t.c_str(), "menuentry ", 10)) { start = (int)i; entries_before = seen_entries++; }
        if (start >= 0 && strstr(t.c_str(), sys::GRUB_KERNEL)) {
            for (size_t j = i; j < lines.size(); j++)
                if (trim(lines[j].c_str()) == "}") { end = (int)j; break; }
        }
    }
    if (start < 0 || end < 0) return false;
    Vec<String> block;
    block.push(String());
    block.push(to_utf8(String::format("menuentry \"%s\" {", T("BrysOS 98 (versión anterior)"))));
    for (int i = start + 1; i <= end; i++) {
        String l = lines[(size_t)i];
        if (const char* k = strstr(l.c_str(), sys::GRUB_KERNEL)) {
            size_t pos = (size_t)(k - l.c_str());
            l = l.substr(0, pos) + sys::GRUB_KERNEL_OLD + l.substr(pos + strlen(sys::GRUB_KERNEL)).c_str();
            // sin la presentación con sonido
            if (const char* in = strstr(l.c_str(), " intro")) {
                size_t p = (size_t)(in - l.c_str());
                if (p + 6 == l.size() || l[p + 6] == ' ') l.erase(p, 6);
            }
        }
        block.push(move_val(l));
    }
    for (size_t i = 0; i < block.size(); i++) lines.insert((size_t)end + 1 + i, block[i]);
    // fallback y un tiempo de espera mínimo para poder elegirla
    int fallback = entries_before + 1;
    bool have_fb = false;
    int default_line = -1;
    for (size_t i = 0; i < lines.size(); i++) {
        String t = trim(lines[i].c_str());
        if (!strncmp(t.c_str(), "set fallback=", 13)) { lines[i] = String::format("set fallback=%d", fallback); have_fb = true; }
        if (!strncmp(t.c_str(), "set default=", 12) && default_line < 0) default_line = (int)i;
        if (!strncmp(t.c_str(), "set timeout=", 12)) {
            int v = 0;
            for (const char* p = t.c_str() + 12; *p >= '0' && *p <= '9'; p++) v = v * 10 + (*p - '0');
            if (v < 3) lines[i] = "set timeout=3";
        }
    }
    if (!have_fb) lines.insert(default_line >= 0 ? (size_t)default_line + 1 : 0, String::format("set fallback=%d", fallback));
    String out;
    for (size_t i = 0; i < lines.size(); i++) {
        out += lines[i];
        if (i + 1 < lines.size()) out += "\n";
    }
    return vfs::write_file(sys::GRUB_CFG, out.c_str(), (uint32_t)out.size());
}

struct Journal {
    String version, name;
    int phase = 1;
    Vec<String> deletes;
    Vec<String> files;   // índice = archivo F<i>.TMP del directorio temporal
};

String journal_text(const Journal& j) {
    String t = String::format("NAME=%s\r\nVERSION=%s\r\nPHASE=%d\r\n", j.name.c_str(), j.version.c_str(), j.phase);
    for (auto& d : j.deletes) { t += "DELETE="; t += d; t += "\r\n"; }
    for (auto& f : j.files) { t += "FILE="; t += f; t += "\r\n"; }
    return t;
}

bool read_journal(Journal& j) {
    uint8_t* data;
    uint32_t size;
    if (!vfs::read_file(sys::UPDATE_JOURNAL, data, size)) return false;
    Vec<String> lines = split_lines((const char*)data, size);
    mm::kfree(data);
    for (auto& l : lines) {
        const char* s = l.c_str();
        if (!strncmp(s, "NAME=", 5)) j.name = s + 5;
        else if (!strncmp(s, "VERSION=", 8)) j.version = s + 8;
        else if (!strncmp(s, "PHASE=", 6)) j.phase = s[6] - '0';
        else if (!strncmp(s, "DELETE=", 7)) j.deletes.push(String(s + 7));
        else if (!strncmp(s, "FILE=", 5)) j.files.push(String(s + 5));
    }
    return true;
}

// Sustituye los archivos. Se puede repetir sin peligro si se interrumpe: primero los
// borrados (fase 1) y después, uno a uno, los archivos preparados que aún no se han movido.
int commit(Journal& j) {
    int failures = 0;
    if (j.phase == 1) {
        for (auto& d : j.deletes) {
            String p = full_path(d);
            if (vfs::exists(p.c_str()) && !vfs::remove_tree(p.c_str())) {
                klog("Actualización: no se pudo borrar %s: %s\n", p.c_str(), vfs::error());
                failures++;
            }
        }
        j.phase = 2;
        String t = journal_text(j);
        vfs::write_file(sys::UPDATE_JOURNAL, t.c_str(), (uint32_t)t.size());
    }
    for (size_t i = 0; i < j.files.size(); i++) {
        String staged = staged_path(i);
        if (!vfs::exists(staged.c_str())) continue;   // ya colocado
        String target = full_path(j.files[i]);
        if (!make_dirs(vfs::parent(target.c_str())) ||
            (vfs::exists(target.c_str()) && !vfs::remove(target.c_str())) ||
            !vfs::rename(staged.c_str(), target.c_str())) {
            klog("Actualización: no se pudo colocar %s: %s\n", target.c_str(), vfs::error());
            failures++;
        }
    }
    return failures;
}

void sync_system() {
    for (Volume* v : vfs::volumes()) if (!v->read_only) v->fs->sync();
    for (BlockDevice* d : block::disks()) d->flush();
}

void finish(const Journal& j, int files, int deletes) {
    String ver = j.version + "\r\n";
    vfs::write_file(sys::VERSION, ver.c_str(), (uint32_t)ver.size());
    // GRUB debe cargar las bibliotecas que haya ahora en C:\BrysOS\BSL
    settings::update_boot_line();
    // registro de actualizaciones
    String log;
    uint8_t* data;
    uint32_t size;
    if (vfs::read_file(sys::UPDATE_LOG, data, size)) { log = String((const char*)data, size); mm::kfree(data); }
    DateTime t = rtc::now();
    log += String::format("%02d/%02d/%04d %02d:%02d  %s (%s): %d archivo(s), %d borrado(s)\r\n", t.day, t.month, t.year,
                          t.hour, t.minute, j.name.c_str(), j.version.c_str(), files, deletes);
    vfs::write_file(sys::UPDATE_LOG, log.c_str(), (uint32_t)log.size());
    vfs::remove(sys::UPDATE_JOURNAL);
    if (vfs::exists(STAGE)) vfs::remove_tree(STAGE);
    sync_system();
}
}

bsi::Package::~Package() { mm::kfree(raw); }

bool bsi::Package::replaces_kernel() const {
    for (auto& f : files) if (ieq(f.path, "kernel.elf")) return true;
    return false;
}

uint64_t bsi::Package::bytes() const {
    uint64_t n = 0;
    for (auto& f : files) n += f.size;
    return n;
}

int bsi::compare_versions(const char* a, const char* b) {
    auto is_num = [](char c) { return c >= '0' && c <= '9'; };
    // parte numérica: números separados por puntos
    while (is_num(*a) || is_num(*b)) {
        uint32_t x = 0, y = 0;
        while (is_num(*a)) x = x * 10 + (uint32_t)(*a++ - '0');
        while (is_num(*b)) y = y * 10 + (uint32_t)(*b++ - '0');
        if (x != y) return x < y ? -1 : 1;
        if (*a == '.') a++;
        if (*b == '.') b++;
    }
    // sufijo de versión preliminar ("-alfa", "-beta2"): va antes que la versión final
    while (*a == '-' || *a == ' ') a++;
    while (*b == '-' || *b == ' ') b++;
    if (!*a || !*b) return *a ? -1 : (*b ? 1 : 0);
    int c = strcmp(a, b);
    return c < 0 ? -1 : (c > 0 ? 1 : 0);
}

String bsi::installed_version() {
    uint8_t* data;
    uint32_t size;
    if (vfs::read_file(sys::VERSION, data, size)) {
        String v = trim(String((const char*)data, size).c_str());
        mm::kfree(data);
        int nl = v.find('\n');
        if (nl >= 0) v = trim(v.substr(0, (size_t)nl).c_str());
        if (!v.empty()) return v;
    }
    return BRYSOS_VERSION;
}

bool bsi::load(const char* path, Package& p, String& err) {
    VfsEntry st;
    if (!vfs::stat(path, st)) { err = vfs::error(); return false; }
    if (st.size < 32 || st.size > MAX_PACKAGE) { err = T("El archivo no es un paquete de actualización de BrysOS válido."); return false; }
    if (!vfs::read_file(path, p.raw, p.raw_size)) { err = vfs::error(); return false; }
    const uint8_t* h = p.raw;
    if (memcmp(h, MAGIC, 8) || rd32(h + 28) != part::crc32(h, 28)) {
        err = T("El archivo no es un paquete de actualización de BrysOS válido.");
        return false;
    }
    if (rd32(h + 8) != FORMAT) {
        err = T("El paquete usa un formato más moderno que esta versión de BrysOS no conoce.");
        return false;
    }
    uint32_t msize = rd32(h + 12), psize = rd32(h + 20);
    if ((uint64_t)32 + msize + psize != p.raw_size) { err = T("El paquete está incompleto o dañado (tamaño incorrecto)."); return false; }
    const char* man = (const char*)h + 32;
    if (part::crc32(man, msize) != rd32(h + 16)) { err = T("El paquete está dañado (el índice no coincide con su suma de control)."); return false; }
    p.payload = h + 32 + msize;
    p.payload_size = psize;

    Vec<String> lines = split_lines(man, msize);
    for (auto& l : lines) {
        const char* s = l.c_str();
        const char* eq = strchr(s, '=');
        if (!*s || *s == '#' || !eq) continue;
        String key(s, (size_t)(eq - s));
        const char* val = eq + 1;
        if (key == "NAME") p.name = val;
        else if (key == "VERSION") p.version = val;
        else if (key == "MIN_VERSION") p.min_version = val;
        else if (key == "DESCRIPTION") { if (!p.description.empty()) p.description += "\n"; p.description += val; }
        else if (key == "REBOOT") p.reboot = val[0] == '1';
        else if (key == "DELETE") {
            String c;
            if (!clean_path(String(val), c, true, err)) return false;
            p.deletes.push(move_val(c));
        } else if (key == "FILE") {
            // ruta|desplazamiento|tamaño|crc
            String v = val;
            int b3 = v.rfind('|');
            if (b3 < 0) { err = T("El índice del paquete está mal formado."); return false; }
            String crc_s = v.substr((size_t)b3 + 1);
            v = v.substr(0, (size_t)b3);
            int b2 = v.rfind('|');
            if (b2 < 0) { err = T("El índice del paquete está mal formado."); return false; }
            String size_s = v.substr((size_t)b2 + 1);
            v = v.substr(0, (size_t)b2);
            int b1 = v.rfind('|');
            if (b1 < 0) { err = T("El índice del paquete está mal formado."); return false; }
            String off_s = v.substr((size_t)b1 + 1);
            String rel = v.substr(0, (size_t)b1);
            FileEntry f;
            if (!clean_path(rel, f.path, false, err)) return false;
            uint64_t off = 0, sz = 0;
            for (const char* q = off_s.c_str(); *q; q++) off = off * 10 + (uint64_t)(*q - '0');
            for (const char* q = size_s.c_str(); *q; q++) sz = sz * 10 + (uint64_t)(*q - '0');
            for (const char* q = crc_s.c_str(); *q; q++) {
                char c = to_upper(*q);
                f.crc = (f.crc << 4) | (uint32_t)(c >= 'A' ? c - 'A' + 10 : c - '0');
            }
            if (off + sz > psize) { err = String::format(T("El paquete está dañado: \"%s\" queda fuera de los datos."), rel.c_str()); return false; }
            f.offset = (uint32_t)off;
            f.size = (uint32_t)sz;
            if (part::crc32(p.payload + f.offset, f.size) != f.crc) {
                err = String::format(T("El paquete está dañado: el archivo \"%s\" no coincide con su suma de control."), f.path.c_str());
                return false;
            }
            for (auto& o : p.files)
                if (ieq(o.path, f.path.c_str())) { err = String::format(T("El paquete contiene dos veces \"%s\"."), f.path.c_str()); return false; }
            p.files.push(move_val(f));
        }
    }
    if (p.version.empty()) { err = T("El paquete no indica qué versión instala."); return false; }
    if (p.name.empty()) p.name = String("BrysOS ") + p.version;
    if (p.files.empty() && p.deletes.empty()) { err = T("El paquete está vacío."); return false; }
    return true;
}

bool bsi::apply(const Package& p, ProgressFn progress, void* ctx, String& err) {
    auto step = [&](int pct, const char* what) { if (progress) progress(ctx, pct, what); };
    if (vfs::system_on_ram()) {
        err = T("BrysOS no está instalado en este equipo (se está ejecutando desde el medio de instalación).");
        return false;
    }
    if (vfs::exists(sys::UPDATE_JOURNAL)) {
        err = T("Hay otra actualización a medio aplicar. Reinicie el equipo para que termine.");
        return false;
    }
    Volume* c = vfs::volume('C');
    VfsEntry kst;
    bool have_kernel = vfs::stat(sys::KERNEL, kst);
    uint64_t need = p.bytes() + (p.replaces_kernel() && have_kernel ? kst.size : 0) + 256 * 1024;
    if (!c || c->fs->free_bytes() < need) {
        err = String::format(T("No hay espacio suficiente en C: (hacen falta %s)."), vfs::format_size(need).c_str());
        return false;
    }
    for (auto& f : p.files) {
        String t = full_path(f.path);
        if (vfs::is_dir(t.c_str())) {
            err = String::format(T("Ya existe una carpeta llamada \"%s\"; el paquete quiere poner un archivo en su lugar."), f.path.c_str());
            return false;
        }
    }

    vfs::SystemWrite sw;
    // 1. Copia de los archivos nuevos a una carpeta temporal, comprobando cada uno
    if (vfs::exists(STAGE)) vfs::remove_tree(STAGE);
    if (!vfs::mkdir(STAGE)) { err = vfs::error(); return false; }
    auto fail = [&](const String& why) {
        err = why;
        vfs::remove_tree(STAGE);
        sync_system();
        return false;
    };
    for (size_t i = 0; i < p.files.size(); i++) {
        const FileEntry& f = p.files[i];
        step(5 + (int)(i * 55 / p.files.size()), String::format(T("Copiando %s..."), f.path.c_str()).c_str());
        if (!write_verified(staged_path(i).c_str(), p.payload + f.offset, f.size))
            return fail(String::format(T("No se pudo copiar \"%s\" al disco: %s"), f.path.c_str(), vfs::error()));
    }
    // 2. El núcleo actual se conserva como kernel.old y GRUB ofrece arrancarlo
    if (p.replaces_kernel() && have_kernel) {
        step(65, T("Guardando una copia del núcleo actual..."));
        uint8_t* k;
        uint32_t ksize;
        if (!vfs::read_file(sys::KERNEL, k, ksize)) return fail(String(vfs::error()));
        bool ok = write_verified(sys::KERNEL_OLD, k, ksize);
        mm::kfree(k);
        if (!ok) return fail(String::format(T("No se pudo guardar la copia del núcleo: %s"), vfs::error()));
        if (vfs::exists(sys::GRUB_CFG)) {
            step(70, T("Añadiendo la versión anterior al menú de arranque..."));
            if (!add_grub_fallback()) klog("Actualización: no se pudo añadir kernel.old a grub.cfg\n");
        }
    }
    // 3. Diario: a partir de aquí, si se va la luz, el siguiente arranque termina el trabajo
    Journal j;
    j.name = p.name;
    j.version = p.version;
    for (auto& d : p.deletes) j.deletes.push(d);
    for (auto& f : p.files) j.files.push(f.path);
    String jt = journal_text(j);
    if (!vfs::write_file(sys::UPDATE_JOURNAL, jt.c_str(), (uint32_t)jt.size()))
        return fail(String::format(T("No se pudo escribir el diario de la actualización: %s"), vfs::error()));
    sync_system();
    // 4. Sustitución
    step(80, T("Sustituyendo los archivos del sistema..."));
    int failures = commit(j);
    step(95, T("Terminando..."));
    finish(j, (int)p.files.size(), (int)p.deletes.size());
    klog("Actualización %s (%s) aplicada: %d archivo(s), %d borrado(s), %d error(es)\n", p.name.c_str(), p.version.c_str(),
         (int)p.files.size(), (int)p.deletes.size(), failures);
    if (failures) {
        err = String::format(T("La actualización se aplicó, pero %d archivo(s) no se pudieron sustituir. "
                               "La orden REGISTRO del Símbolo del sistema muestra cuáles."), failures);
        return false;
    }
    step(100, T("Actualización completada."));
    return true;
}

void bsi::recover() {
    if (vfs::system_on_ram()) return;
    vfs::SystemWrite sw;
    if (!vfs::exists(sys::UPDATE_JOURNAL)) {
        // restos de una actualización que no llegó a empezar a sustituir nada
        if (vfs::exists(STAGE)) {
            klog("Actualización: se borran los restos de una actualización cancelada\n");
            vfs::remove_tree(STAGE);
            sync_system();
        }
        return;
    }
    Journal j;
    if (!read_journal(j)) return;
    klog("Actualización: se termina de aplicar %s (%s), interrumpida\n", j.name.c_str(), j.version.c_str());
    int failures = commit(j);
    finish(j, (int)j.files.size(), (int)j.deletes.size());
    if (failures) klog("Actualización: %d archivo(s) no se pudieron sustituir\n", failures);
}

// Símbolo del sistema: intérprete de órdenes al estilo MS-DOS sobre el VFS real
#include "acpi/power.h"
#include "apps/common.h"
#include "fs/fat32.h"
#include "drivers/pci.h"
#include "drivers/block.h"
#include "drivers/usb.h"
#include "lib/log.h"
#include "drivers/input.h"
#include "mm/memory.h"
#include "arch/idt.h"
#include "kernel.h"
#include "bsl/loader.h"
#include "drivers/acpi.h"
#include "drivers/audio.h"

namespace {
constexpr int MAX_LINES = 1000;

class Console : public Window {
public:
    Vec<String> lines;
    String input;
    int cur = 0;                 // posición del cursor en la entrada
    String cwd = "C:\\";
    Vec<String> history;
    int hist_pos = -1;
    int view_off = 0;            // filas desplazadas hacia arriba desde el final
    bool caret = true;
    String* capture = nullptr;   // redirección de salida
    ScrollBar vs;
    int sb = 0;
    int batch_depth = 0;

    Console(const char* dir) : Window(T("Símbolo del sistema"), 640, 400, WF_NORMAL) {
        icon = &icon_cmd;
        min_w = 260;
        min_h = 120;
        vs.vertical = true;
        if (dir && vfs::is_dir(dir)) cwd = vfs::normalize(dir);
        print("BrysOS [Versión " BRYSOS_VERSION "]\n");
        print(T("(C) BrysOS. Escriba AYUDA para ver la lista de órdenes.\n\n"));
        wm::set_timer(this, 1, 500);
    }

    // ------------------------------------------------------------------ salida
    void print(const char* s) {
        if (capture) { *capture += s; return; }
        if (lines.empty()) lines.push(String());
        for (; *s; s++) {
            if (*s == '\r') continue;
            if (*s == '\n') { lines.push(String()); continue; }
            if (*s == '\t') { lines.back() += "    "; continue; }
            lines.back().append(*s);
        }
        while (lines.size() > MAX_LINES) lines.erase(0);
        view_off = 0;
        invalidate();
    }
    void printf(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
        char buf[1024];
        va_list ap;
        va_start(ap, fmt);
        kvsnprintf(buf, sizeof buf, fmt, ap);
        va_end(ap);
        print(buf);
    }
    String prompt() { return cwd + ">"; }

    // ------------------------------------------------------------------ dibujo
    int cols() const { return max(10, (client_w() - 20) / font_mono.char_width('M')); }
    int rows() const { return max(1, (client_h() - 4) / font_mono.height); }

    // Filas visuales (con ajuste de línea), incluida la línea de entrada
    void visual_rows(Vec<String>& out, int& caret_row, int& caret_col) {
        int c = cols();
        for (size_t i = 0; i < lines.size(); i++) {
            String l = lines[i];
            if (i + 1 == lines.size()) {
                // última línea: se le añade el indicador y la entrada
                caret_row = -1;
                if (!busy_running) {
                    String full = l + prompt() + input;
                    size_t caret_pos = l.size() + prompt().size() + (size_t)cur;
                    size_t start = 0;
                    do {
                        String part = full.substr(start, (size_t)c);
                        if (caret_pos >= start && caret_pos < start + (size_t)c) { caret_row = (int)out.size(); caret_col = (int)(caret_pos - start); }
                        out.push(part);
                        start += (size_t)c;
                    } while (start < full.size() || (start == full.size() && caret_pos == full.size() && caret_row < 0));
                    if (caret_row < 0) { caret_row = (int)out.size() - 1; caret_col = (int)(caret_pos - (start - (size_t)c)); }
                    continue;
                }
            }
            if (l.empty()) { out.push(String()); continue; }
            for (size_t s = 0; s < l.size(); s += (size_t)c) out.push(l.substr(s, (size_t)c));
        }
    }

    bool busy_running = false;

    void on_paint(Surface& s) override {
        int w = client_w(), h = client_h();
        s.fill({0, 0, w, h}, col::BLACK);
        Vec<String> vr;
        int crow = -1, ccol = 0;
        visual_rows(vr, crow, ccol);
        int rws = rows();
        int total = (int)vr.size();
        vs.r = {w - 16, 0, 16, h};
        vs.total = total;
        vs.page = rws;
        vs.line = 1;
        int first = max(0, total - rws - view_off);
        vs.pos = first;
        int cw = font_mono.char_width('M');
        for (int i = 0; i < rws && first + i < total; i++)
            s.text(font_mono, 2, 2 + i * font_mono.height, vr[(size_t)(first + i)].c_str(), 0xC0C0C0);
        if (caret && crow >= first && crow < first + rws && wm::active() == this && !busy_running)
            s.fill({2 + ccol * cw, 2 + (crow - first) * font_mono.height + font_mono.height - 3, cw, 2}, 0xC0C0C0);
        vs.paint(s);
    }

    void on_timer(int) override { caret = !caret; invalidate(); }

    void on_mouse_wheel(const MouseEv& e) override {
        Vec<String> vr; int a, b;
        visual_rows(vr, a, b);
        int maxoff = max(0, (int)vr.size() - rows());
        view_off = clamp(view_off - e.wheel * 3, 0, maxoff);
        invalidate();
    }

    void on_mouse_down(const MouseEv& e) override {
        bool ch;
        if (vs.mouse_down(e.x, e.y, ch)) {
            sb = 1;
            view_off = max(0, vs.total - vs.page - vs.pos);
            invalidate();
        } else if (e.button == MB_RIGHT && !wm::clipboard().empty()) {
            // clic derecho: pegar
            for (size_t i = 0; i < wm::clipboard().size(); i++) {
                char c = wm::clipboard()[i];
                if (c == '\r' || c == '\n') break;
                input.insert((size_t)cur++, &c, 1);
            }
            invalidate();
        }
    }
    void on_mouse_move(const MouseEv& e) override {
        if (!sb) return;
        bool ch;
        vs.mouse_move(e.x, e.y, ch);
        view_off = max(0, vs.total - vs.page - vs.pos);
        if (ch) invalidate();
    }
    void on_mouse_up(const MouseEv&) override { sb = 0; vs.mouse_up(); invalidate(); }

    // ------------------------------------------------------------------ teclado
    bool on_key(const InputEvent& e) override {
        if (!e.pressed) return false;
        caret = true;
        if (e.mods & MOD_CTRL) {
            if (e.key == 'v') {
                for (size_t i = 0; i < wm::clipboard().size(); i++) {
                    char c = wm::clipboard()[i];
                    if (c == '\r' || c == '\n') break;
                    input.insert((size_t)cur++, &c, 1);
                }
                invalidate();
                return true;
            }
            if (e.key == 'c') { print(String(prompt() + input + T("^C\n")).c_str()); input.clear(); cur = 0; return true; }
            return false;
        }
        switch (e.key) {
        case KEY_ENTER: {
            String line = input;
            print((prompt() + input + T("\n")).c_str());
            input.clear();
            cur = 0;
            if (!line.empty()) {
                if (history.empty() || history.back() != line.c_str()) history.push(line);
            }
            hist_pos = -1;
            busy_running = true;
            execute(line.c_str());
            busy_running = false;
            if (closing) return true;
            if (!lines.back().empty()) print(T("\n"));
            print(T("\n"));
            return true;
        }
        case KEY_BACKSPACE: if (cur > 0) { input.erase((size_t)cur - 1, 1); cur--; } break;
        case KEY_DELETE: if (cur < (int)input.size()) input.erase((size_t)cur, 1); break;
        case KEY_LEFT: if (cur > 0) cur--; break;
        case KEY_RIGHT: if (cur < (int)input.size()) cur++; break;
        case KEY_HOME: cur = 0; break;
        case KEY_END: cur = (int)input.size(); break;
        case KEY_ESC: input.clear(); cur = 0; break;
        case KEY_UP:
            if (history.empty()) return true;
            hist_pos = hist_pos < 0 ? (int)history.size() - 1 : max(0, hist_pos - 1);
            input = history[(size_t)hist_pos];
            cur = (int)input.size();
            break;
        case KEY_DOWN:
            if (hist_pos < 0) return true;
            if (++hist_pos >= (int)history.size()) { hist_pos = -1; input.clear(); }
            else input = history[(size_t)hist_pos];
            cur = (int)input.size();
            break;
        case KEY_PGUP: on_mouse_wheel(MouseEv{.wheel = -rows() / 3 * 1}); break;
        case KEY_PGDN: on_mouse_wheel(MouseEv{.wheel = rows() / 3}); break;
        default:
            if (e.ch >= 32) { char c = (char)e.ch; input.insert((size_t)cur++, &c, 1); }
            else return false;
        }
        view_off = 0;
        invalidate();
        return true;
    }

    // ------------------------------------------------------------------ órdenes
    static void split_args(const char* s, Vec<String>& out) {
        out.clear();
        while (*s) {
            while (*s == ' ' || *s == '\t') s++;
            if (!*s) break;
            String a;
            if (*s == '"') {
                s++;
                while (*s && *s != '"') a.append(*s++);
                if (*s == '"') s++;
            } else {
                while (*s && *s != ' ' && *s != '\t') {
                    if (*s == '"') { s++; while (*s && *s != '"') a.append(*s++); if (*s) s++; continue; }
                    a.append(*s++);
                }
            }
            out.push(move_val(a));
        }
    }

    String full(const String& p) { return vfs::normalize(p.c_str(), cwd.c_str()); }

    bool has_wild(const String& p) {
        for (size_t i = 0; i < p.size(); i++) if (p[i] == '*' || p[i] == '?') return true;
        return false;
    }

    // Expande comodines del último componente
    void expand(const String& arg, Vec<String>& out) {
        out.clear();
        String f = full(arg);
        if (!has_wild(f)) { out.push(f); return; }
        String dir = vfs::parent(f.c_str()), pat = vfs::basename(f.c_str());
        Vec<VfsEntry> ents;
        if (!vfs::list(dir.c_str(), ents)) return;
        for (auto& e : ents)
            if (wildcard_match(pat.c_str(), e.name.c_str())) out.push(vfs::join(dir.c_str(), e.name.c_str()));
    }

    void execute(const char* raw) {
        // Redirección: orden > archivo / orden >> archivo
        String cmdline = raw;
        String redir;
        bool append = false;
        int gt = -1;
        bool inq = false;
        for (size_t i = 0; i < cmdline.size(); i++) {
            if (cmdline[i] == '"') inq = !inq;
            if (cmdline[i] == '>' && !inq) { gt = (int)i; break; }
        }
        if (gt >= 0) {
            size_t p = (size_t)gt + 1;
            if (p < cmdline.size() && cmdline[p] == '>') { append = true; p++; }
            Vec<String> r;
            split_args(cmdline.c_str() + p, r);
            if (r.empty()) { print(T("Error de sintaxis.\n")); return; }
            redir = full(r[0]);
            cmdline = cmdline.substr(0, (size_t)gt);
        }
        String out;
        if (!redir.empty()) capture = &out;
        run(cmdline.c_str());
        if (!redir.empty()) {
            capture = nullptr;
            String data;
            if (append) {
                uint8_t* d;
                uint32_t n;
                if (vfs::read_file(redir.c_str(), d, n)) { data.append((const char*)d, n); mm::kfree(d); }
            }
            // la salida usa saltos de línea de DOS
            for (size_t i = 0; i < out.size(); i++) {
                if (out[i] == '\n') data += T("\r\n");
                else data.append(out[i]);
            }
            if (!vfs::write_file(redir.c_str(), data.c_str(), (uint32_t)data.size())) printf(T("%s\n"), vfs::error());
        }
    }

    void run(const char* line) {
        Vec<String> a;
        split_args(line, a);
        if (a.empty()) return;
        String c = a[0];
        for (size_t i = 0; i < c.size(); i++) c[i] = to_upper(c[i]);
        // "C:" cambia de unidad
        if (c.size() == 2 && c[1] == ':') {
            if (!vfs::volume(c[0])) { print(T("La unidad especificada no es válida\n")); return; }
            cwd = String(c.c_str()) + "\\";
            return;
        }
        if (c == "CD.." ) { cmd_cd("..", true); return; }
        if (c == "CD\\") { cmd_cd("\\", true); return; }
        if (c == "AYUDA" || c == "HELP" || c == "/?") { cmd_help(); return; }
        if (c == "CLS") { lines.clear(); lines.push(String()); return; }
        if (c == "VER") { print("\nBrysOS [Versión " BRYSOS_VERSION "]\n"); return; }
        if (c == "ECHO") {
            if (a.size() == 1) { print(T("ECHO está activado.\n")); return; }
            const char* p = line;
            while (*p == ' ') p++;
            p += 4;
            if (*p == ' ' || *p == '.') p++;
            printf(T("%s\n"), p);
            return;
        }
        if (c == "CD" || c == "CHDIR") {
            // como en DOS, el resto de la línea es la ruta (admite espacios sin comillas)
            const char* p = line;
            while (*p == ' ') p++;
            while (*p && *p != ' ') p++;
            while (*p == ' ') p++;
            String rest = p;
            while (rest.size() && rest[rest.size() - 1] == ' ') rest.erase(rest.size() - 1, 1);
            if (rest.size() >= 2 && rest[0] == '"') rest = rest.substr(1, rest.find('"', 1) > 0 ? (size_t)rest.find('"', 1) - 1 : rest.size() - 1);
            cmd_cd(rest.empty() ? nullptr : rest.c_str(), true);
            return;
        }
        if (c == "DIR") { cmd_dir(a); return; }
        if (c == "MD" || c == "MKDIR") { need(a, 2) && (vfs::mkdir(full(a[1]).c_str()) || err()); return; }
        if (c == "RD" || c == "RMDIR") { cmd_rd(a); return; }
        if (c == "DEL" || c == "ERASE" || c == "BORRAR") { cmd_del(a); return; }
        if (c == "REN" || c == "RENAME") { cmd_ren(a); return; }
        if (c == "COPY" || c == "COPIAR") { cmd_copy(a, false); return; }
        if (c == "MOVE" || c == "MOVER") { cmd_copy(a, true); return; }
        if (c == "TYPE") { cmd_type(a); return; }
        if (c == "VOL") { cmd_vol(a.size() > 1 ? a[1][0] : cwd[0]); return; }
        if (c == "LABEL") { cmd_label(a); return; }
        if (c == "DATE" || c == "FECHA") {
            DateTime t = rtc::now();
            printf(T("La fecha actual es: %s %02d/%02d/%04d\n"), T(weekday_name(t.weekday())), t.day, t.month, t.year);
            return;
        }
        if (c == "TIME" || c == "HORA") {
            DateTime t = rtc::now();
            printf(T("La hora actual es: %d:%02d:%02d\n"), t.hour, t.minute, t.second);
            return;
        }
        if (c == "REGISTRO" || c == "LOG") {
            // mensajes del núcleo desde el arranque (detección de hardware, discos...)
            print(log_buffer());
            return;
        }
        if (c == "MEM") {
            printf(T("\n%12lu KB de memoria total\n%12lu KB libres\n%12lu KB usados por el montón del kernel\n"),
                   mm::total_memory() / 1024, mm::free_memory() / 1024, mm::heap_used() / 1024);
            printf(T("%12lu segundos desde el arranque\n"), pit::ticks() / 1000);
            return;
        }
        if (c == "TREE" || c == "ARBOL") { cmd_tree(a); return; }
        if (c == "ATTRIB") { cmd_attrib(a); return; }
        if (c == "DISPOSITIVOS" || c == "LSPCI") { cmd_devices(); return; }
        if (c == "BSL" || c == "BIBLIOTECAS") { cmd_bsl(); return; }
        if (c == "ACPI") { cmd_acpi(a); return; }
        if (c == "ENERGIA" || c == "BATERIA" || c == "POWER") {
            power::poll();
            String rep;
            power::diagnostics(rep);
            print(rep.c_str());
            String dir = a.size() >= 2 ? full(a[1]) : String();
            if (!dir.empty()) {
                if (!vfs::is_dir(dir.c_str())) vfs::mkdir(dir.c_str());
                if (vfs::write_file(vfs::join(dir.c_str(), "energia.txt").c_str(), rep.c_str(), (uint32_t)rep.size()))
                    printf(T("Informe guardado en %s\n"), vfs::join(dir.c_str(), "energia.txt").c_str());
                else err();
            }
            return;
        }
        if (c == "SONIDO" || c == "SOUND") {
            String rep;
            audio::diagnostics(rep);
            print(rep.c_str());
            String dir = a.size() >= 2 ? full(a[1]) : String();
            if (!dir.empty()) {
                if (!vfs::is_dir(dir.c_str())) vfs::mkdir(dir.c_str());
                if (vfs::write_file(vfs::join(dir.c_str(), "sonido.txt").c_str(), rep.c_str(), (uint32_t)rep.size()))
                    printf(T("Informe guardado en %s\n"), vfs::join(dir.c_str(), "sonido.txt").c_str());
                else err();
            }
            return;
        }
        if (c == "EXIT" || c == "SALIR") { close(); return; }
        if (c == "START") {
            if (!need(a, 2)) return;
            String rest = line;
            int sp = rest.find(' ');
            if (!apps::run_command(rest.substr((size_t)sp + 1).c_str())) print(T("No se puede encontrar el programa.\n"));
            return;
        }
        // Archivo por lotes
        String f = full(a[0]);
        if (vfs::extension(f.c_str()).empty() && vfs::exists((f + T(".bat")).c_str())) f += T(".bat");
        if (vfs::extension(f.c_str()) == "BAT" && vfs::exists(f.c_str())) { run_batch(f.c_str()); return; }
        // Aplicaciones gráficas y archivos
        if (apps::run_command(line)) return;
        if (vfs::exists(f.c_str()) && !vfs::is_dir(f.c_str())) { apps::open_file(f.c_str()); return; }
        printf(T("\"%s\" no se reconoce como una orden interna o externa,\nprograma o archivo por lotes.\n"), a[0].c_str());
    }

    bool need(const Vec<String>& a, size_t n) {
        if (a.size() >= n) return true;
        print(T("Falta un parámetro obligatorio.\n"));
        return false;
    }
    bool err() { printf(T("%s\n"), vfs::error()); return false; }

    void cmd_help() {
        print(T("Órdenes disponibles:\n\n"
              "ACPI [carpeta]                Guarda las tablas ACPI del equipo (en el pendrive)\n"
              "ATTRIB [+R|-R] [+H|-H] arch.  Muestra o cambia atributos\n"
              "BSL                           Lista las bibliotecas del sistema cargadas\n"
              "CD [ruta]                     Muestra o cambia la carpeta actual\n"
              "CLS                           Borra la pantalla\n"
              "COPY origen destino           Copia archivos (admite * y ?)\n"
              "DATE / TIME                   Muestra la fecha / la hora\n"
              "DEL archivo                   Elimina archivos (admite * y ?)\n"
              "DIR [ruta] [/W]               Muestra el contenido de una carpeta\n"
              "DISPOSITIVOS                  Lista el hardware PCI y los discos\n"
              "ECHO texto                    Muestra un mensaje\n"
              "EXIT                          Cierra el símbolo del sistema\n"
              "LABEL [unidad:] [etiqueta]    Cambia la etiqueta del volumen\n"
              "MD carpeta                    Crea una carpeta\n"
              "MEM                           Muestra el uso de memoria\n"
              "MOVE origen destino           Mueve archivos o carpetas\n"
              "RD [/S] carpeta               Elimina una carpeta\n"
              "REGISTRO                      Muestra el registro de arranque\n"
              "REN archivo nuevo_nombre      Cambia el nombre\n"
              "SONIDO [carpeta]              Prueba el sonido e informa del códec\n"
              "START programa                Inicia un programa\n"
              "TREE [ruta]                   Muestra el árbol de carpetas\n"
              "TYPE archivo                  Muestra el contenido de un archivo\n"
              "VER / VOL                     Versión del sistema / del volumen\n\n"
              "Programas: NOTEPAD, PAINT, CALC, EXPLORER, WINMINE, BUSCAR, AYUDA\n"
              "Redirección: orden > archivo, orden >> archivo. Archivos .BAT.\n"));
    }

    void cmd_cd(const char* arg, bool) {
        if (!arg) { printf(T("%s\n"), cwd.c_str()); return; }
        String n = full(arg);
        if (!vfs::is_dir(n.c_str())) { print(T("El sistema no puede encontrar la ruta especificada.\n")); return; }
        // usa las mayúsculas reales del disco
        cwd = n;
    }

    void cmd_vol(char letter) {
        Volume* v = vfs::volume(letter);
        if (!v) { print(T("La unidad especificada no es válida\n")); return; }
        if (v->label.empty()) printf(T(" El volumen de la unidad %c no tiene etiqueta.\n"), v->letter);
        else printf(T(" El volumen de la unidad %c es %s\n"), v->letter, v->label.c_str());
    }

    void cmd_label(const Vec<String>& a) {
        char letter = cwd[0];
        size_t i = 1;
        if (a.size() > 1 && a[1].size() >= 2 && a[1][1] == ':') { letter = a[1][0]; i = 2; }
        Volume* v = vfs::volume(letter);
        if (!v) { print(T("La unidad especificada no es válida\n")); return; }
        if (i >= a.size()) { cmd_vol(letter); return; }
        if (v->read_only) { print(T("La unidad es de sólo lectura.\n")); return; }
        if (!stricmp(v->label.c_str(), "BRYSOS") && stricmp(a[i].c_str(), "BRYSOS")) {
            print(T("Aviso: si cambia la etiqueta BRYSOS, la unidad dejará de usarse como C: al reiniciar.\n"));
        }
        if (v->fs->set_label(a[i].c_str())) v->label = v->fs->label;
        else print(T("No se pudo cambiar la etiqueta.\n"));
    }

    void cmd_dir(const Vec<String>& a) {
        bool wide = false;
        String target = cwd;
        String pattern = "*";
        for (size_t i = 1; i < a.size(); i++) {
            if (!stricmp(a[i].c_str(), "/W")) wide = true;
            else if (a[i][0] != '/') target = a[i];
        }
        String f = full(target);
        if (has_wild(f)) { pattern = vfs::basename(f.c_str()); f = vfs::parent(f.c_str()); }
        else if (vfs::exists(f.c_str()) && !vfs::is_dir(f.c_str())) { pattern = vfs::basename(f.c_str()); f = vfs::parent(f.c_str()); }
        Vec<VfsEntry> ents;
        if (!vfs::list(f.c_str(), ents)) { print(T("No se encuentra el archivo\n")); return; }
        ents.sort([](const VfsEntry& x, const VfsEntry& y) {
            if (x.dir != y.dir) return x.dir ? -1 : 1;
            return stricmp(x.name.c_str(), y.name.c_str());
        });
        cmd_vol(f[0]);
        printf(T("\n Directorio de %s\n\n"), f.c_str());
        int files = 0, dirs = 0;
        uint64_t bytes = 0;
        int col = 0;
        for (auto& e : ents) {
            if (!wildcard_match(pattern.c_str(), e.name.c_str())) continue;
            if (e.dir) dirs++; else { files++; bytes += e.size; }
            if (wide) {
                String n = e.dir ? String::format(T("[%s]"), e.name.c_str()) : e.name;
                printf(T("%-20s"), n.c_str());
                if (++col == 4) { print(T("\n")); col = 0; }
            } else {
                const DateTime& t = e.mtime;
                if (e.dir) printf(T("%02d/%02d/%04d  %02d:%02d    <DIR>          %s\n"), t.day, t.month, t.year, t.hour, t.minute, e.name.c_str());
                else printf(T("%02d/%02d/%04d  %02d:%02d    %14s %s\n"), t.day, t.month, t.year, t.hour, t.minute,
                            group(e.size).c_str(), e.name.c_str());
            }
        }
        if (wide && col) print(T("\n"));
        Volume* v = vfs::volume(f[0]);
        printf(T("%16d archivos %15s bytes\n"), files, group(bytes).c_str());
        printf(T("%16d dirs     %15s bytes libres\n"), dirs, group(v ? v->fs->free_bytes() : 0).c_str());
    }

    static String group(uint64_t v) {
        char tmp[32];
        int n = ksnprintf(tmp, sizeof tmp, T("%lu"), v);
        String out;
        for (int i = 0; i < n; i++) {
            out.append(tmp[i]);
            int rest = n - 1 - i;
            if (rest && rest % 3 == 0) out.append('.');
        }
        return out;
    }

    void cmd_rd(const Vec<String>& a) {
        bool tree = false;
        String target;
        for (size_t i = 1; i < a.size(); i++) {
            if (!stricmp(a[i].c_str(), "/S")) tree = true;
            else target = a[i];
        }
        if (target.empty()) { print(T("Falta un parámetro obligatorio.\n")); return; }
        String f = full(target);
        if (!vfs::is_dir(f.c_str())) { print(T("El sistema no puede encontrar la ruta especificada.\n")); return; }
        if (!stricmp(f.c_str(), cwd.c_str())) { print(T("No se puede eliminar la carpeta actual.\n")); return; }
        bool ok = tree ? vfs::remove_tree(f.c_str()) : vfs::remove(f.c_str());
        if (!ok) err();
    }

    void cmd_del(const Vec<String>& a) {
        if (!need(a, 2)) return;
        Vec<String> files;
        expand(a[1], files);
        if (files.empty()) { print(T("No se encuentra el archivo\n")); return; }
        if (files.size() > 1 && vfs::basename(full(a[1]).c_str()) == "*.*") {
            print(T("Se eliminarán todos los archivos de la carpeta.\n"));
        }
        int n = 0;
        for (auto& f : files) {
            if (vfs::is_dir(f.c_str())) continue;
            if (vfs::remove(f.c_str())) n++;
            else printf(T("%s: %s\n"), vfs::basename(f.c_str()).c_str(), vfs::error());
        }
        if (files.size() > 1) printf(T("%d archivo(s) eliminado(s).\n"), n);
    }

    void cmd_ren(const Vec<String>& a) {
        if (!need(a, 3)) return;
        String src = full(a[1]);
        String dst = vfs::join(vfs::parent(src.c_str()).c_str(), a[2].c_str());
        if (!vfs::rename(src.c_str(), dst.c_str())) err();
    }

    void cmd_copy(const Vec<String>& a, bool move) {
        if (!need(a, 2)) return;
        Vec<String> files;
        expand(a[1], files);
        if (files.empty()) { print(T("No se encuentra el archivo\n")); return; }
        String dst = a.size() > 2 ? full(a[2]) : cwd;
        bool dst_dir = vfs::is_dir(dst.c_str());
        int n = 0;
        for (auto& f : files) {
            if (!move && vfs::is_dir(f.c_str())) continue;
            String target = dst_dir ? vfs::join(dst.c_str(), vfs::basename(f.c_str()).c_str()) : dst;
            if (!stricmp(target.c_str(), f.c_str())) { print(T("Un archivo no se puede copiar sobre sí mismo.\n")); continue; }
            if (!move && vfs::exists(target.c_str())) vfs::remove(target.c_str());
            bool ok = move ? vfs::rename(f.c_str(), target.c_str()) : vfs::copy(f.c_str(), target.c_str());
            if (ok) { n++; if (files.size() > 1) printf(T("%s\n"), vfs::basename(f.c_str()).c_str()); }
            else printf(T("%s: %s\n"), vfs::basename(f.c_str()).c_str(), vfs::error());
        }
        printf(T("%8d archivo(s) %s.\n"), n, move ? T("movido(s)") : T("copiado(s)"));
    }

    void cmd_type(const Vec<String>& a) {
        if (!need(a, 2)) return;
        Vec<String> files;
        expand(a[1], files);
        if (files.empty()) { print(T("No se encuentra el archivo\n")); return; }
        for (auto& f : files) {
            uint8_t* d;
            uint32_t n;
            if (!vfs::read_file(f.c_str(), d, n)) { err(); continue; }
            if (files.size() > 1) printf(T("\n%s\n\n"), vfs::basename(f.c_str()).c_str());
            for (uint32_t i = 0; i < n; i++) if (d[i] == 0) d[i] = ' ';
            print((const char*)d);
            if (n && d[n - 1] != '\n') print(T("\n"));
            mm::kfree(d);
        }
    }

    void tree_rec(const String& dir, const String& prefix, int depth) {
        if (depth > 16) return;
        Vec<VfsEntry> ents;
        if (!vfs::list(dir.c_str(), ents)) return;
        Vec<String> subs;
        for (auto& e : ents) if (e.dir) subs.push(e.name);
        subs.sort([](const String& x, const String& y) { return stricmp(x.c_str(), y.c_str()); });
        for (size_t i = 0; i < subs.size(); i++) {
            bool last = i + 1 == subs.size();
            printf(T("%s%s%s\n"), prefix.c_str(), last ? "\\---" : "+---", subs[i].c_str());
            tree_rec(vfs::join(dir.c_str(), subs[i].c_str()), prefix + (last ? "    " : "|   "), depth + 1);
        }
    }

    void cmd_tree(const Vec<String>& a) {
        String d = a.size() > 1 ? full(a[1]) : cwd;
        if (!vfs::is_dir(d.c_str())) { print(T("Ruta no válida\n")); return; }
        printf(T("Listado de rutas de carpetas\n%s\n"), d.c_str());
        tree_rec(d, "", 0);
    }

    void cmd_attrib(const Vec<String>& a) {
        uint8_t set = 0, clr = 0;
        String target = "*";
        for (size_t i = 1; i < a.size(); i++) {
            const String& s = a[i];
            if ((s[0] == '+' || s[0] == '-') && s.size() == 2) {
                uint8_t bit = to_upper(s[1]) == 'R' ? FAT_RO : to_upper(s[1]) == 'H' ? FAT_HIDDEN :
                              to_upper(s[1]) == 'S' ? FAT_SYSTEM : to_upper(s[1]) == 'A' ? FAT_ARCHIVE : 0;
                if (s[0] == '+') set |= bit; else clr |= bit;
            } else {
                target = s;
            }
        }
        Vec<String> files;
        expand(target, files);
        for (auto& f : files) {
            VfsEntry e;
            if (!vfs::stat(f.c_str(), e)) continue;
            if (set || clr) {
                if (!vfs::set_attr(f.c_str(), (uint8_t)((e.attr | set) & ~clr))) err();
            } else {
                printf(T("%c  %c%c%c   %s\n"), e.attr & FAT_ARCHIVE ? 'A' : ' ', e.attr & FAT_SYSTEM ? 'S' : ' ',
                       e.attr & FAT_HIDDEN ? 'H' : ' ', e.attr & FAT_RO ? 'R' : ' ', f.c_str());
            }
        }
    }

    void cmd_devices() {
        print(T("\nDispositivos PCI:\n"));
        for (auto& d : pci::devices())
            printf(T("  %02x:%02x.%u  %04x:%04x  %s\n"), d.bus, d.dev, d.func, d.vendor, d.device, T(pci::class_name(d.cls, d.subclass)));
        print(T("\nDiscos:\n"));
        for (BlockDevice* d : block::disks())
            printf(T("  %-6s %-30s %s%s\n"), d->name, d->model, vfs::format_size(d->sectors * 512).c_str(),
                   d->removable ? T(" (extraíble)") : "");
        print(T("\nUSB:\n"));
        if (!usb::device_count()) print(T("  (ninguno)\n"));
        for (int i = 0; i < usb::device_count(); i++) printf(T("  %s\n"), usb::describe(i));
        printf(T("\nFirmware: %s\nPuntero: %s\n"), firmware_name(), T(ps2::pointer_name()));
    }

    // Bibliotecas del sistema cargadas
    void cmd_bsl() {
        print(T("\nBibliotecas del sistema (.bsl):\n"));
        if (bsl::libraries().empty()) print(T("  (ninguna)\n"));
        for (auto* l : bsl::libraries())
            printf(T("  %-12s %-6s %4lu KB  %s\n            %s\n"), l->file.c_str(), l->version.c_str(), l->size >> 10,
                   l->origin.c_str(), l->description.c_str());
        for (auto& e : bsl::errors()) printf(T("  ERROR: %s\n"), e.c_str());
    }

    // Guarda las tablas ACPI del firmware, la lista de dispositivos y el registro de arranque
    // (para estudiar el equipo: batería, brillo...). Por omisión, en el primer pendrive.
    void cmd_acpi(const Vec<String>& a) {
        String dir;
        if (a.size() >= 2) dir = full(a[1]);
        else {
            for (Volume* v : vfs::volumes())
                if (v->removable && !v->read_only) { dir = String::format("%c:\\ACPI", v->letter); break; }
            if (dir.empty()) dir = "C:\\ACPI";
        }
        if (!vfs::is_dir(dir.c_str()) && !vfs::mkdir(dir.c_str())) { err(); return; }
        int saved = 0;
        char sig[5];
        const uint8_t* data;
        uint32_t len;
        String list;
        for (int i = 0; i < acpi::table_count(); i++) {
            if (!acpi::table_at(i, sig, data, len)) continue;
            for (char& ch : sig) if (ch && !((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9'))) ch = '_';
            String name = vfs::join(dir.c_str(), (String(sig) + ".DAT").c_str());
            for (int k = 2; vfs::exists(name.c_str()); k++)
                name = vfs::join(dir.c_str(), String::format("%s%d.DAT", sig, k).c_str());
            if (!vfs::write_file(name.c_str(), data, len)) { err(); return; }
            list += String::format("%s  %u bytes\r\n", vfs::basename(name.c_str()).c_str(), len);
            saved++;
        }
        // dispositivos PCI (con identificadores, para saber qué tarjeta gráfica y qué controladoras hay)
        String dev;
        for (auto& d : pci::devices())
            dev += String::format("%02x:%02x.%u  %04x:%04x  clase %02x.%02x.%02x  %s\r\n", d.bus, d.dev, d.func, d.vendor, d.device,
                                  d.cls, d.subclass, d.prog_if, pci::class_name(d.cls, d.subclass));
        vfs::write_file(vfs::join(dir.c_str(), "pci.txt").c_str(), dev.c_str(), (uint32_t)dev.size());
        const char* log = log_buffer();
        vfs::write_file(vfs::join(dir.c_str(), "registro.txt").c_str(), log, (uint32_t)strlen(log));
        vfs::write_file(vfs::join(dir.c_str(), "tablas.txt").c_str(), list.c_str(), (uint32_t)list.size());
        String snd;
        audio::diagnostics(snd);
        vfs::write_file(vfs::join(dir.c_str(), "sonido.txt").c_str(), snd.c_str(), (uint32_t)snd.size());
        printf(T("Se han guardado %d tablas ACPI, la lista de dispositivos y el registro en %s\n"), saved, dir.c_str());
    }

    void run_batch(const char* path) {
        if (batch_depth > 8) { print(T("Demasiados archivos por lotes anidados.\n")); return; }
        uint8_t* d;
        uint32_t n;
        if (!vfs::read_file(path, d, n)) { err(); return; }
        batch_depth++;
        char* p = (char*)d;
        while (*p) {
            char* l = p;
            while (*p && *p != '\n') p++;
            if (*p) *p++ = 0;
            String line = l;
            while (line.size() && (line[line.size() - 1] == '\r' || line[line.size() - 1] == ' ')) line.erase(line.size() - 1, 1);
            const char* s = line.c_str();
            while (*s == ' ') s++;
            if (!*s || !strncmp(s, "::", 2)) continue;
            String up = s;
            for (size_t i = 0; i < up.size() && i < 4; i++) up[i] = to_upper(up[i]);
            if (!strncmp(up.c_str(), "REM", 3) && (up.size() == 3 || up[3] == ' ')) continue;
            if (*s == '@') s++;
            if (!stricmp(s, "ECHO OFF")) continue;
            execute(s);
            if (closing) break;
        }
        batch_depth--;
        mm::kfree(d);
    }
};
}

namespace apps {
void cmd(const char* dir) {
    wm::add(new Console(dir));
}
}

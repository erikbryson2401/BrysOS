// Ejecutar, Apagar el sistema, Panel de control, Propiedades y Ayuda
#include "apps/common.h"
#include "apps/settings.h"
#include "fs/fat32.h"
#include "drivers/acpi.h"
#include "drivers/input.h"
#include "gui/desktop.h"
#include "drivers/rtc.h"
#include "drivers/keyboard.h"
#include "mm/memory.h"
#include "kernel.h"
#include "arch/io.h"
#include "drivers/audio.h"

extern "C" const uint8_t intro_pcm_start[], intro_pcm_end[];

namespace {
// ---------------------------------------------------------------------------
// Ejecutar
// ---------------------------------------------------------------------------
String last_run = T("notepad");

class RunDlg : public Dialog {
public:
    TextBox* tb;
    RunDlg() : Dialog(T("Ejecutar"), 390, 130) {
        add(new Label({52, 12, 330, 32}, T("Escriba el nombre de un programa, carpeta o documento\ny BrysOS lo abrirá.")));
        add(new Label({12, 60, 40, 16}, T("&Abrir:")));
        tb = add(new TextBox({52, 56, 326, 21}, last_run.c_str()));
        default_btn = add(new Button({140, 96, 75, 23}, T("Aceptar"), ID_OK));
        default_btn->is_default = true;
        add(new Button({221, 96, 75, 23}, T("Cancelar"), ID_CANCEL));
        add(new Button({302, 96, 76, 23}, T("&Examinar..."), 50));
        focus = tb;
    }
    void on_paint(Surface& s) override {
        s.clear(col::FACE);
        draw_icon(s, icon_run.big, 12, 10);
    }
    void on_command(int id, int code) override {
        if (id == 50) {
            String p = "C:\\";
            if (file_dialog(this, false, T("Examinar"), nullptr, p)) tb->set_text(p.c_str());
            return;
        }
        Dialog::on_command(id, code);
    }
    bool on_ok() override {
        if (tb->text.empty()) return false;
        last_run = tb->text;
        return true;
    }
};

// ---------------------------------------------------------------------------
// Apagar
// ---------------------------------------------------------------------------
class ShutdownDlg : public Dialog {
public:
    CheckBox* off;
    CheckBox* restart;
    ShutdownDlg() : Dialog(T("Apagar el sistema"), 320, 150) {
        add(new Label({60, 14, 250, 16}, T("¿Qué desea que haga el equipo?")));
        off = add(new CheckBox({60, 40, 240, 20}, T("&Apagar el equipo"), 60, true));
        off->radio = true;
        restart = add(new CheckBox({60, 64, 240, 20}, T("&Reiniciar el equipo"), 61, false));
        restart->radio = true;
        default_btn = add(new Button({76, 114, 75, 23}, T("Aceptar"), ID_OK));
        default_btn->is_default = true;
        add(new Button({157, 114, 75, 23}, T("Cancelar"), ID_CANCEL));
        focus = default_btn;
    }
    void on_paint(Surface& s) override {
        s.clear(col::FACE);
        draw_icon(s, icon_shutdown.big, 14, 14);
    }
    void on_command(int id, int code) override {
        if (id == 60) { off->checked = true; restart->checked = false; invalidate(); return; }
        if (id == 61) { off->checked = false; restart->checked = true; invalidate(); return; }
        Dialog::on_command(id, code);
    }
};

bool close_all_apps() {
    // Pide a cada aplicación que se cierre (pueden preguntar si guardar)
    Vec<Window*> list = wm::windows();
    for (size_t i = list.size(); i > 0; i--) {
        Window* w = list[i - 1];
        if (w->flags & (WF_DESKTOP | WF_POPUP | WF_NO_TASKBAR)) continue;
        if (w == (Window*)taskbar()) continue;
        if (wm::windows().index_of(w) < 0) continue;
        wm::activate(w);
        if (!w->on_close()) return false;
        wm::destroy(w);
    }
    return true;
}

[[noreturn]] void final_screen(bool reboot) {
    for (Volume* v : vfs::volumes()) if (!v->read_only) v->fs->sync();
    Surface& s = gfx::backbuffer();
    s.reset_clip();
    if (reboot) acpi::reboot();
    s.clear(0x000000);
    const char* t = T("Espere mientras se apaga el equipo...");
    s.text(font_large, (s.w - font_large.text_width(t)) / 2, s.h / 2 - 10, t, 0xFFFFFF);
    gfx::present_all();
    acpi::shutdown();
    // Si el firmware no permite apagar por software
    s.clear(0x000000);
    const char* l1 = T("Ahora puede apagar el equipo con seguridad.");
    s.text(font_large, (s.w - font_large.text_width(l1)) / 2, s.h / 2 - 12, l1, 0xFF8000);
    gfx::present_all();
    halt_forever();
}

// ---------------------------------------------------------------------------
// Panel de control
// ---------------------------------------------------------------------------
struct NamedColor { const char* name; uint32_t c; };
const NamedColor colors[] = {
    {"Verde azulado", 0x008080}, {"Azul marino", 0x000080}, {"Azul cielo", 0x3A6EA5},
    {"Verde oscuro", 0x004000}, {"Burdeos", 0x800000}, {"Púrpura", 0x400040}, {"Gris", 0x808080},
    {"Negro", 0x000000}, {"Arena", 0xA08050},
};

void cpu_brand(char out[49]) {
    uint32_t regs[12];
    uint32_t a, b, c, d;
    asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x80000000));
    if (a < 0x80000004) {
        asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0));
        memcpy(out, &b, 4); memcpy(out + 4, &d, 4); memcpy(out + 8, &c, 4);
        out[12] = 0;
        return;
    }
    for (uint32_t i = 0; i < 3; i++)
        asm volatile("cpuid" : "=a"(regs[i * 4]), "=b"(regs[i * 4 + 1]), "=c"(regs[i * 4 + 2]), "=d"(regs[i * 4 + 3]) : "a"(0x80000002 + i));
    memcpy(out, regs, 48);
    out[48] = 0;
    char* p = out;
    while (*p == ' ') p++;
    memmove(out, p, strlen(p) + 1);
}

class ControlPanel : public Dialog {
public:
    ListView* list;
    Vec<CheckBox*> speed;
    uint32_t chosen;
    int chosen_speed;

    ControlPanel() : Dialog(T("Panel de control"), 440, 584) {
        icon = &icon_settings;
        flags &= ~WF_NO_TASKBAR;
        chosen = settings::desktop_color;
        chosen_speed = settings::mouse_speed;
        add(new Label({16, 16, 200, 16}, T("&Color del escritorio:")));
        list = add(new ListView({16, 34, 200, 156}, 70));
        list->mode = ListView::LIST;
        for (auto& c : colors) list->add(T(c.name), nullptr, (int64_t)c.c);
        add(new Button({16, 196, 118, 23}, T("&Personalizado..."), 71));
        add(new Button({138, 196, 78, 23}, T("&Fondo..."), 79));
        add(new Label({240, 16, 180, 16}, T("&Velocidad del puntero:")));
        const char* names[4] = {"Lenta", "Normal", "Rápida", "Muy rápida"};
        for (int i = 0; i < 4; i++) {
            CheckBox* cb = add(new CheckBox({248, 36 + i * 22, 160, 20}, T(names[i]), 80 + i, chosen_speed == i + 1));
            cb->radio = true;
            speed.push(cb);
        }
        add(new Label({16, 236, 200, 16}, T("&Idioma:")));
        lang = add(new ListView({16, 254, 200, 46}, 72));
        lang->mode = ListView::LIST;
        lang->add("Español", nullptr);
        lang->add("English", nullptr);
        lang->select(!strcmp(settings::language, "en") ? 1 : 0);
        add(new Label({240, 236, 180, 16}, T("&Teclado:")));
        kbd = add(new ListView({240, 254, 188, 62}, 73));
        kbd->mode = ListView::LIST;
        for (int i = 0; i < keyboard::layout_count(); i++) {
            kbd->add(T(keyboard::layout(i).name), nullptr);
            if (!strcmp(keyboard::layout(i).id, settings::keyboard)) kbd->select(i);
        }
        intro = add(new CheckBox({16, 324, 410, 20}, T("Mostrar el logo con el &sonido de inicio en cada arranque"), 74, settings::intro_always));
        // Sonido
        add(new Label({24, 374, 130, 16}, T("&Volumen general:")));
        vol = add(new TrackBar({150, 362, 196, 40}, 75));
        vol->set(settings::volume);
        mute = add(new CheckBox({356, 372, 70, 20}, T("Silen&cio"), 76, settings::muted));
        add(new Label({24, 418, 130, 16}, T("Sonido de i&nicio:")));
        intro_vol = add(new TrackBar({150, 406, 196, 40}, 77));
        intro_vol->set(settings::intro_volume);
        add(new Button({356, 414, 66, 23}, T("P&robar"), 78));
        default_btn = add(new Button({192, 548, 75, 23}, T("Aceptar"), ID_OK));
        default_btn->is_default = true;
        add(new Button({273, 548, 75, 23}, T("Cancelar"), ID_CANCEL));
        add(new Button({354, 548, 75, 23}, T("A&plicar"), 90));
    }

    TrackBar* vol;
    TrackBar* intro_vol;
    CheckBox* mute;

    // Al cancelar se vuelve al volumen guardado (el deslizador se oye en el acto)
    bool on_close() override {
        audio::stop();
        audio::set_volume(settings::volume);
        audio::set_mute(settings::muted);
        return true;
    }

    ListView* lang;
    ListView* kbd;
    CheckBox* intro;

    void on_paint(Surface& s) override {
        s.clear(col::FACE);
        // Vista previa (monitor)
        Rect m{248, 132, 170, 92};
        s.fill(m, col::FACE);
        s.bevel_raised(m);
        Rect scr = m.inset(8);
        s.fill(scr, chosen);
        s.bevel_sunken(scr.inset(-2));
        s.fill({scr.x + 20, scr.y + 14, 60, 36}, col::FACE);
        s.gradient_h({scr.x + 22, scr.y + 16, 56, 8}, col::TITLE_A1, col::TITLE_A2);
        s.fill({scr.x, scr.bottom() - 8, scr.w, 8}, col::FACE);
        // Sonido
        Rect snd{12, 352, 416, 104};
        s.etched(snd);
        s.fill({snd.x + 8, snd.y - 6, 52, 12}, col::FACE);
        s.text(font_ui, snd.x + 10, snd.y - 7, T("Sonido"), col::BLACK);
        if (!audio::available())
            s.text(font_ui, snd.x + 12, snd.bottom() - 18, T("No se ha encontrado ninguna tarjeta de sonido compatible."), col::SHADOW);
        // Información del sistema
        Rect g{12, 456 + 10, 416, 76};
        s.etched(g);
        s.fill({g.x + 8, g.y - 6, 60, 12}, col::FACE);
        s.text(font_ui, g.x + 10, g.y - 7, T("Sistema"), col::BLACK);
        char cpu[49];
        cpu_brand(cpu);
        int y = g.y + 10;
        s.text(font_ui, g.x + 12, y, String::format(T("BrysOS %s (64 bits), firmware %s"), BRYSOS_VERSION, firmware_name()).c_str(), 0); y += 15;
        s.text(font_ui, g.x + 12, y, String::format(T("Procesador: %s"), cpu).c_str(), 0); y += 15;
        s.text(font_ui, g.x + 12, y, String::format(T("Memoria: %lu MB   Pantalla: %dx%d"), mm::total_memory() >> 20, wm::screen_w(), wm::screen_h()).c_str(), 0); y += 15;
        s.text(font_ui, g.x + 12, y, String::format(T("Dispositivo señalador: %s"), T(ps2::pointer_name())).c_str(), 0);
    }

    void apply() {
        settings::desktop_color = chosen;
        settings::mouse_speed = chosen_speed;
        bool lang_changed = false;
        if (lang->selected() >= 0) {
            const char* code = lang->selected() == 1 ? "en" : "es";
            lang_changed = strcmp(code, settings::language) != 0;
            strlcpy(settings::language, code, sizeof settings::language);
        }
        if (kbd->selected() >= 0) strlcpy(settings::keyboard, keyboard::layout(kbd->selected()).id, sizeof settings::keyboard);
        bool intro_changed = intro->checked != settings::intro_always;
        settings::intro_always = intro->checked;
        bool sound_changed = vol->value != settings::volume || mute->checked != settings::muted ||
                             intro_vol->value != settings::intro_volume;
        settings::volume = vol->value;
        settings::muted = mute->checked;
        settings::intro_volume = intro_vol->value;
        settings::apply();
        // grub.cfg lleva el idioma de los mensajes de arranque, el volumen y la presentación
        if (intro_changed) settings::set_boot_intro(settings::intro_always);
        else if (lang_changed || sound_changed) settings::update_boot_line();
        if (taskbar()) taskbar()->invalidate();
        if (lang_changed) {
            // lenguajes.bsl traduce al momento todas las ventanas abiertas, menús incluidos
            if (taskbar()) taskbar()->rebuild();
            if (desktop()) desktop()->rebuild();
            wm::retranslate_all();
        }
        if (!settings::save()) msgbox(this, T("Panel de control"), String::format(T("No se pudo guardar la configuración: %s"), vfs::error()).c_str(), MB_OK, &icon_warning);
    }

    bool on_ok() override { apply(); return true; }

    void on_command(int id, int code) override {
        if (id == 70 && code == LV_SELCHANGE && list->selected() >= 0) { chosen = (uint32_t)list->items[list->selected()].data; invalidate(); return; }
        if (id == 71) {
            String v = String::format("%06X", chosen);
            if (input_box(this, T("Color personalizado"), T("Color en hexadecimal (RRGGBB):"), v)) {
                uint32_t n = 0;
                for (const char* p = v.c_str(); *p; p++) {
                    char c = to_upper(*p);
                    int d = (c >= '0' && c <= '9') ? c - '0' : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
                    if (d < 0) break;
                    n = n * 16 + d;
                }
                chosen = n & 0xFFFFFF;
                invalidate();
            }
            return;
        }
        if (id >= 80 && id < 84) {
            chosen_speed = id - 79;
            for (int i = 0; i < 4; i++) speed[i]->checked = i == id - 80;
            invalidate();
            return;
        }
        if (id == 90) { apply(); return; }
        if (id == 79) { apps::wallpaper_dialog(); return; }
        if (id == 75) { audio::set_volume(vol->value); return; }     // se oye al momento
        if (id == 76) { audio::set_mute(mute->checked); return; }
        if (id == 78) {
            // el sonido de inicio con los volúmenes elegidos
            size_t frames = (size_t)(intro_pcm_end - intro_pcm_start) / 4;
            audio::set_volume(vol->value);
            audio::set_mute(mute->checked);
            if (!audio::play((const int16_t*)intro_pcm_start, frames, intro_vol->value))
                msgbox(this, T("Panel de control"), T("No se ha encontrado ninguna tarjeta de sonido compatible."), MB_OK, &icon_warning);
            return;
        }
        if (id == 77) { if (audio::playing()) audio::set_gain(intro_vol->value); return; }
        if (id == ID_CANCEL) on_close();
        Dialog::on_command(id, code);
    }
};

// ---------------------------------------------------------------------------
// Propiedades
// ---------------------------------------------------------------------------
class PropsDlg : public Dialog {
public:
    String path;
    bool is_drive;
    VfsEntry info;
    uint64_t total_size = 0;
    int nfiles = 0, ndirs = 0;
    CheckBox* ro = nullptr;
    CheckBox* hid = nullptr;
    CheckBox* arc = nullptr;
    TextBox* label = nullptr;
    Volume* vol = nullptr;

    PropsDlg(const char* p) : Dialog(T("Propiedades"), 340, 380), path(vfs::normalize(p)) {
        flags &= ~WF_NO_TASKBAR;
        is_drive = path.size() <= 3;
        vfs::stat(path.c_str(), info);
        String name = is_drive ? path : vfs::basename(path.c_str());
        set_title(String::format(T("Propiedades de %s"), name.c_str()).c_str());
        if (is_drive) {
            vol = vfs::volume(path[0]);
            icon = &icon_hdd;
            add(new Label({16, 22, 60, 16}, T("Etiqueta:")));
            label = add(new TextBox({80, 18, 160, 21}, vol ? vol->label.c_str() : ""));
            label->read_only = !vol || vol->read_only;
        } else {
            icon = icon_for(name.c_str(), info.dir);
            if (info.dir) {
                wm::set_busy(true);
                count(path);
                wm::set_busy(false);
            } else {
                total_size = info.size;
            }
            int y = 254;
            ro = add(new CheckBox({90, y, 110, 20}, T("&Sólo lectura"), 0, info.attr & FAT_RO));
            hid = add(new CheckBox({90, y + 22, 110, 20}, T("&Oculto"), 0, info.attr & FAT_HIDDEN));
            arc = add(new CheckBox({210, y, 110, 20}, T("&Modificado"), 0, info.attr & FAT_ARCHIVE));
        }
        default_btn = add(new Button({92, 344, 75, 23}, T("Aceptar"), ID_OK));
        default_btn->is_default = true;
        add(new Button({173, 344, 75, 23}, T("Cancelar"), ID_CANCEL));
        add(new Button({254, 344, 75, 23}, T("A&plicar"), 95));
    }

    void count(const String& d) {
        Vec<VfsEntry> ents;
        if (!vfs::list(d.c_str(), ents)) return;
        for (auto& e : ents) {
            if (e.dir) { ndirs++; count(vfs::join(d.c_str(), e.name.c_str())); }
            else { nfiles++; total_size += e.size; }
        }
    }

    void row(Surface& s, int& y, const char* k, const char* v) {
        s.text(font_ui, 16, y, k, col::BLACK);
        s.text(font_ui, 100, y, v, col::BLACK);
        y += 20;
    }

    void on_paint(Surface& s) override {
        s.clear(col::FACE);
        if (is_drive) {
            if (!vol) return;
            int y = 56;
            s.hline(12, y - 6, client_w() - 24, col::SHADOW);
            s.hline(12, y - 5, client_w() - 24, col::HILIGHT);
            row(s, y, T("Tipo:"), vol->ram ? T("Disco en memoria") : T("Disco local"));
            row(s, y, T("Sistema:"), "FAT32");
            uint64_t total = vol->fs->total_bytes(), free = vol->fs->free_bytes(), used = total - free;
            y += 6;
            s.fill({16, y + 2, 12, 12}, 0x0000FF); s.rect({16, y + 2, 12, 12}, 0);
            row(s, y, T("    Usado:"), String::format(T("%s"), vfs::format_size(used).c_str()).c_str());
            s.fill({16, y + 2, 12, 12}, 0xFF00FF); s.rect({16, y + 2, 12, 12}, 0);
            row(s, y, T("    Libre:"), vfs::format_size(free).c_str());
            row(s, y, T("Capacidad:"), vfs::format_size(total).c_str());
            // Gráfico circular del espacio usado (sólo aritmética entera)
            int cx = client_w() / 2, cy = y + 70, rx = 70, ry = 45, depth = 12;
            uint64_t frac = total ? used * 1024 / total : 0;
            for (int d = depth; d >= 0; d--) {
                for (int py = -ry; py <= ry; py++)
                    for (int px = -rx; px <= rx; px++) {
                        if ((int64_t)px * px * ry * ry + (int64_t)py * py * rx * rx > (int64_t)rx * rx * ry * ry) continue;
                        // ángulo aproximado medido en sentido horario desde las 12
                        int64_t ax = (int64_t)px * ry, ay = (int64_t)py * rx;
                        uint64_t ang;
                        // pseudoángulo monótono 0..4096 sin trigonometría
                        int64_t sum = (ax < 0 ? -ax : ax) + (ay < 0 ? -ay : ay);
                        int64_t pa = sum ? 1024 * ax / sum : 0;  // -1024..1024
                        if (ay < 0) ang = (uint64_t)(pa >= 0 ? pa : 4096 + pa);
                        else ang = (uint64_t)(2048 - pa);
                        bool used_px = ang * 1024 < frac * 4096;
                        uint32_t c = used_px ? 0x0000FF : 0xFF00FF;
                        if (d > 0) c = used_px ? 0x000080 : 0x800080;
                        s.put(cx + px, cy + py + d, c);
                    }
            }
            s.text_centered(font_ui, {0, cy + ry + depth + 6, client_w(), 16}, String::format(T("Unidad %c"), vol->letter).c_str(), 0);
            return;
        }
        draw_icon(s, icon->big, 16, 12);
        s.text(font_ui, 60, 22, vfs::basename(path.c_str()).c_str(), col::BLACK);
        s.hline(12, 52, client_w() - 24, col::SHADOW);
        s.hline(12, 53, client_w() - 24, col::HILIGHT);
        int y = 62;
        row(s, y, T("Tipo:"), type_name(path.c_str(), info.dir).c_str());
        row(s, y, T("Ubicación:"), vfs::parent(path.c_str()).c_str());
        String sz = String::format(T("%s (%lu bytes)"), vfs::format_size(total_size).c_str(), total_size);
        row(s, y, T("Tamaño:"), sz.c_str());
        if (info.dir) row(s, y, T("Contiene:"), String::format(T("%d archivos, %d carpetas"), nfiles, ndirs).c_str());
        s.hline(12, y + 2, client_w() - 24, col::SHADOW);
        s.hline(12, y + 3, client_w() - 24, col::HILIGHT);
        y += 12;
        row(s, y, T("Creado:"), format_date(info.ctime).c_str());
        row(s, y, T("Modificado:"), format_date(info.mtime).c_str());
        s.hline(12, 244, client_w() - 24, col::SHADOW);
        s.hline(12, 245, client_w() - 24, col::HILIGHT);
        s.text(font_ui, 16, 256, T("Atributos:"), col::BLACK);
    }

    bool apply() {
        if (is_drive) {
            if (vol && !vol->read_only && label && label->text != vol->label.c_str()) {
                if (!stricmp(vol->label.c_str(), "BRYSOS") && stricmp(label->text.c_str(), "BRYSOS") &&
                    msgbox(this, T("Propiedades"), T("Si cambia la etiqueta BRYSOS, BrysOS no usará esta unidad como C: en el próximo arranque.\n\n¿Desea continuar?"),
                           MB_YESNO, &icon_warning) != ID_YES)
                    return false;
                if (vol->fs->set_label(label->text.c_str())) vol->label = vol->fs->label;
            }
            return true;
        }
        uint8_t a = info.attr & ~(FAT_RO | FAT_HIDDEN | FAT_ARCHIVE);
        if (ro->checked) a |= FAT_RO;
        if (hid->checked) a |= FAT_HIDDEN;
        if (arc->checked) a |= FAT_ARCHIVE;
        if (a != info.attr) {
            if (!vfs::set_attr(path.c_str(), a)) { msgbox(this, T("Propiedades"), vfs::error(), MB_OK, &icon_error); return false; }
            info.attr = a;
        }
        return true;
    }

    bool on_ok() override { return apply(); }
    void on_command(int id, int code) override {
        if (id == 95) { apply(); return; }
        Dialog::on_command(id, code);
    }
};

// ---------------------------------------------------------------------------
// Ayuda
// ---------------------------------------------------------------------------
struct Topic { const char* title; const char* text; };
const Topic topics[] = {
    {"Bienvenido a BrysOS",
     "BrysOS es un sistema operativo de 64 bits escrito desde cero en C++.\n\n"
     "Se instala desde un pendrive y arranca con GRUB (BIOS o UEFI), que permite elegir entre\n"
     "BrysOS y los demás sistemas del equipo. Configura la memoria y las interrupciones del\n"
     "procesador y dibuja su propia interfaz gráfica al estilo de Windows 98.\n\n"
     "Todos los archivos se guardan en la partición FAT32 elegida durante la instalación,\n"
     "que también puede leerse desde Windows o Linux."},
    {"El escritorio y el menú Inicio",
     "- Haga doble clic en un icono del escritorio para abrirlo.\n"
     "- Pulse el botón Inicio (o la tecla Windows, o Ctrl+Esc) para abrir el menú Inicio.\n"
     "- La barra de tareas muestra un botón por cada ventana abierta. Haga clic para activarla o\n"
     "  minimizarla; clic derecho para restaurar, maximizar o cerrar.\n"
     "- Haga doble clic en el reloj para cambiar la fecha, la hora y la zona horaria.\n"
     "- Clic derecho en el escritorio: organizar iconos y propiedades de la pantalla."},
    {"Ventanas",
     "- Arrastre la barra de título para mover una ventana.\n"
     "- Arrastre los bordes o las esquinas para cambiar su tamaño.\n"
     "- Doble clic en la barra de título: maximizar / restaurar.\n"
     "- Alt+F4 cierra la ventana activa; Alt+Tab cambia de ventana.\n"
     "- Alt + letra subrayada abre los menús."},
    {"Explorador y Mi PC",
     "Mi PC muestra las unidades de disco. La unidad C: es la partición donde se instaló BrysOS.\n\n"
     "- Doble clic: abrir carpetas y documentos.\n"
     "- Supr: enviar a la Papelera de reciclaje.\n"
     "- F2: cambiar el nombre.   F5: actualizar.   Retroceso: subir un nivel.\n"
     "- Ctrl+X, Ctrl+C, Ctrl+V: cortar, copiar y pegar archivos.\n"
     "- Menú Ver: iconos grandes, lista o detalles (clic en las columnas para ordenar)."},
    {"Bloc de notas",
     "Editor de texto sin formato. Abre y guarda archivos .TXT en el disco.\n\n"
     "Ctrl+U nuevo, Ctrl+A abrir, Ctrl+G guardar, Ctrl+B buscar, F3 buscar siguiente,\n"
     "Ctrl+T ir a la línea, Ctrl+E seleccionar todo, F5 insertar la hora y la fecha."},
    {"Paint",
     "Editor de mapas de bits. Guarda imágenes BMP de 24 bits y abre BMP de 1, 4, 8, 24 y 32 bits.\n\n"
     "Herramientas: lápiz, pincel, aerógrafo, borrador, relleno, selector de color, línea,\n"
     "rectángulo, elipse y texto. Clic izquierdo dibuja con el color principal y clic derecho\n"
     "con el secundario. Mayús dibuja cuadrados y círculos. Ctrl+Z deshace."},
    {"Calculadora",
     "Calculadora estándar con aritmética decimal exacta de 18 dígitos.\n\n"
     "Puede usar el teclado: números, + - * /, Intro (=), Retroceso, Esc (C) y Supr (CE).\n"
     "La coma o el punto introducen decimales. @ calcula la raíz cuadrada."},
    {"Símbolo del sistema",
     "Intérprete de órdenes al estilo MS-DOS. Escriba AYUDA para ver la lista completa.\n\n"
     "Ejemplos:\n"
     "   DIR C:\\        CD \"Mis documentos\"       TYPE Léame.txt\n"
     "   COPY *.txt C:\\Copias           ECHO Hola > saludo.txt\n"
     "También ejecuta archivos .BAT y programas (NOTEPAD, PAINT, CALC...)."},
    {"Buscar archivos",
     "Inicio > Buscar > Archivos o carpetas.\n\n"
     "Escriba parte del nombre, o un patrón con * y ?, y la carpeta donde buscar.\n"
     "Con el texto: busca dentro del contenido de los archivos."},
    {"Teclado, ratón y touchpad",
     "Distribuciones disponibles: España, Latinoamérica y Estados Unidos. Se cambian en el\n"
     "Panel de control. Las españolas admiten tildes con teclas muertas y AltGr para @ # [ ] { }.\n\n"
     "Funcionan el teclado y el ratón PS/2, y los teclados y ratones USB conectados a\n"
     "controladoras UHCI u OHCI (también a través de concentradores).\n\n"
     "Touchpad Synaptics: toque para hacer clic, toque doble para doble clic, toque y\n"
     "arrastre para mover, y deslice el dedo por el borde derecho para desplazarse.\n"
     "La velocidad del puntero se cambia en el Panel de control."},
    {"Discos y apagado",
     "BrysOS lee y escribe la partición FAT32 en la que se instaló (unidad C:).\n"
     "Las demás particiones FAT32 se muestran como unidades de sólo lectura.\n\n"
     "Para instalar en otro equipo, arranque desde el pendrive de instalación.\n"
     "Use siempre Inicio > Apagar el sistema antes de desconectar el equipo."},
};

class HelpWin : public Window {
public:
    ListView* list;
    TextArea* text;
    HelpWin() : Window(T("Ayuda de BrysOS"), 640, 360, WF_NORMAL) {
        icon = &icon_help;
        list = add(new ListView({0, 0, 10, 10}, 1));
        list->mode = ListView::LIST;
        for (auto& t : topics) list->add(T(t.title), &icon_help);
        list->add(T("Créditos y licencias"), &icon_file_text);   // abre su propia ventana
        text = add(new TextArea({0, 0, 10, 10}, 2));
        text->read_only = true;
        text->font = &font_ui;
        show(0);
        focus = list;
    }
    static constexpr int CREDITS = sizeof(topics) / sizeof(topics[0]);
    void show(int i) {
        list->select(i);
        String t = i == CREDITS
            ? String::format("%s\n\n%s", T("Créditos y licencias"),
                             T("BrysOS incluye componentes de otros proyectos (cargadores de arranque, fuentes,\n"
                               "decodificadores...). Haga doble clic en este tema o pulse Intro para ver sus autores\n"
                               "y las licencias con las que se distribuyen."))
            : String::format("%s\n\n%s", T(topics[i].title), T(topics[i].text));
        text->set_text(t.c_str(), t.size());
    }
    void on_resize() override {
        list->r = {4, 4, 200, client_h() - 8};
        text->r = {208, 4, client_w() - 212, client_h() - 8};
    }
    void on_command(int id, int code) override {
        if (id == 1 && code == LV_SELCHANGE && list->selected() >= 0) show(list->selected());
        if (id == 1 && code == LV_ACTIVATE && list->selected() == CREDITS) apps::credits();
    }
};

// ---------------------------------------------------------------------------
// Propiedades de Fecha y hora
// ---------------------------------------------------------------------------
class DateTimeDlg : public Dialog {
public:
    TextBox* date;
    TextBox* time;
    ListView* zones;
    CheckBox* utc;
    Label* clock;

    DateTimeDlg() : Dialog(T("Propiedades de Fecha y hora"), 420, 340) {
        flags &= ~WF_NO_TASKBAR;
        icon = &icon_settings;
        DateTime t = rtc::now();
        add(new Label({12, 16, 130, 16}, T("&Fecha (dd/mm/aaaa):")));
        date = add(new TextBox({150, 12, 110, 21}, String::format("%02d/%02d/%04d", t.day, t.month, t.year).c_str()));
        add(new Label({12, 46, 130, 16}, T("&Hora (hh:mm):")));
        time = add(new TextBox({150, 42, 110, 21}, String::format(T("%02d:%02d"), t.hour, t.minute).c_str()));
        clock = add(new Label({280, 30, 130, 16}, ""));
        add(new Label({12, 80, 200, 16}, T("&Zona horaria:")));
        zones = add(new ListView({12, 98, 396, 150}, 10));
        zones->mode = ListView::LIST;
        int sel = 0;
        for (int i = 0; i < time_zone_count; i++) {
            zones->add(T(time_zones[i].name), nullptr);
            if (time_zones[i].offset == settings::utc_offset) sel = i;
        }
        zones->select(sel);
        utc = add(new CheckBox({12, 256, 396, 20}, T("El reloj del equipo usa la hora &UTC (si también usa Linux)"), 11, settings::rtc_utc));
        default_btn = add(new Button({172, 306, 75, 23}, T("Aceptar"), ID_OK));
        default_btn->is_default = true;
        add(new Button({253, 306, 75, 23}, T("Cancelar"), ID_CANCEL));
        add(new Button({334, 306, 75, 23}, T("A&plicar"), 12));
        update_clock();
        wm::set_timer(this, 1, 1000);
    }

    void update_clock() {
        DateTime t = rtc::now();
        clock->set_text(String::format(T("Ahora: %d:%02d:%02d"), t.hour, t.minute, t.second).c_str());
    }
    void on_timer(int) override { update_clock(); }

    static bool num(const char*& p, int& v) {
        if (*p < '0' || *p > '9') return false;
        v = 0;
        while (*p >= '0' && *p <= '9') v = v * 10 + (*p++ - '0');
        return true;
    }

    bool apply() {
        int d, m, y, h, mi;
        const char* p = date->text.c_str();
        bool ok = num(p, d) && (*p == '/' || *p == '-') && num(++p, m) && (*p == '/' || *p == '-') && num(++p, y);
        const char* q = time->text.c_str();
        ok = ok && num(q, h) && (*q == ':' || *q == '.') && num(++q, mi);
        if (!ok || y < 2000 || y > 2099 || m < 1 || m > 12 || d < 1 || d > rtc::days_in_month(y, m) || h > 23 || mi > 59) {
            msgbox(this, title, T("Escriba la fecha como dd/mm/aaaa y la hora como hh:mm."), MB_OK, &icon_warning);
            return false;
        }
        int z = max(0, zones->selected());
        settings::utc_offset = time_zones[z].offset;
        settings::rtc_utc = utc->checked;
        rtc::configure(settings::rtc_utc, settings::utc_offset);
        DateTime cur = rtc::now();
        DateTime t{y, m, d, h, mi, cur.second};
        if (t.year != cur.year || t.month != cur.month || t.day != cur.day || t.hour != cur.hour || t.minute != cur.minute)
            rtc::set_local(t);
        settings::save();
        if (taskbar()) taskbar()->invalidate();
        return true;
    }

    bool on_ok() override { return apply(); }
    void on_command(int id, int code) override {
        if (id == 12) { apply(); return; }
        Dialog::on_command(id, code);
    }
};
}

namespace apps {
void datetime_dialog() {
    for (Window* w : wm::windows())
        if (!strcmp(w->title, T("Propiedades de Fecha y hora"))) { wm::activate(w); return; }
    auto* d = new DateTimeDlg();
    d->owner = nullptr;
    d->center();
    wm::add(d);
}

void run_dialog() {
    auto* d = new RunDlg();
    d->frame.x = 8;
    d->frame.y = wm::screen_h() - TASKBAR_H - d->frame.h - 8;
    if (wm::run_modal(d) != ID_OK) return;
    if (!run_command(last_run.c_str()))
        msgbox(nullptr, last_run.c_str(), String::format(T("No se puede encontrar '%s'. Asegúrese de que escribió el nombre correctamente."), last_run.c_str()).c_str(), MB_OK, &icon_error);
}

bool shutdown_open = false;

void shutdown_dialog() {
    if (shutdown_open) return;
    shutdown_open = true;
    auto* d = new ShutdownDlg();
    d->center();
    int r = wm::run_modal(d);
    shutdown_open = false;
    if (r != ID_OK) return;
    bool reboot = d->restart->checked;
    if (!close_all_apps()) return;
    final_screen(reboot);
}

void control_panel() {
    auto* d = new ControlPanel();
    d->owner = nullptr;
    d->center();
    wm::add(d);
}

void properties(const char* path) {
    if (!vfs::exists(path)) { msgbox(nullptr, T("Propiedades"), vfs::error(), MB_OK, &icon_error); return; }
    auto* d = new PropsDlg(path);
    d->owner = nullptr;
    d->center();
    wm::add(d);
}

void help() {
    wm::add(new HelpWin());
}
}

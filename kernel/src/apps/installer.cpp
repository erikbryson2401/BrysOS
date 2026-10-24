// Programa de instalación de BrysOS: idioma, teclado, fecha y hora, partición,
// gestor de arranque GRUB y copia del sistema al disco.
#include "sys/paths.h"
#include "apps/common.h"
#include "apps/settings.h"
#include "fs/partition.h"
#include "fs/bootloader.h"
#include "fs/fat32.h"
#include "drivers/keyboard.h"
#include "drivers/acpi.h"
#include "drivers/rtc.h"
#include "boot/bootinfo.h"
#include "gui/desktop.h"
#include "lib/i18n.h"
#include "kernel.h"

namespace {
enum Page { P_LANG, P_KEYBOARD, P_MODE, P_TIME, P_TARGET, P_BOOT, P_SUMMARY, P_PROGRESS, P_DONE };
enum { ID_BACK = 1, ID_NEXT, ID_CANCEL, ID_LIST, ID_RADIO1, ID_RADIO2, ID_CHECK, ID_LIST2, ID_TEXT };

// Qué representa cada fila de la lista de discos
struct TargetRow {
    enum Kind { DISK, PART, FREE } kind;
    int disk;
    int index;   // partición o hueco
};

class Installer : public Window {
public:
    int page = P_LANG;
    Vec<Widget*> page_widgets;
    Button* back;
    Button* next;
    Button* cancel;

    // elecciones
    bool install = true;
    DateTime date{};
    int zone = 11;
    bool hw_utc = false;
    Vec<DiskLayout> disks;
    Vec<TargetRow> rows;
    int target_row = -1;
    bool keep_data = false;
    String label = "BRYSOS";
    uint64_t new_size_mb = 0;
    bool grub = true;
    int grub_disk = 0;
    int timeout = 10;
    Vec<OtherOs> others;
    String efi_note;
    String backup_name;

    // controles de la página actual
    ListView* list = nullptr;
    ListView* list2 = nullptr;
    TextBox* tb1 = nullptr;
    TextBox* tb2 = nullptr;
    TextBox* tb3 = nullptr;
    CheckBox* r1 = nullptr;
    CheckBox* r2 = nullptr;
    CheckBox* chk = nullptr;
    Label* info = nullptr;
    ProgressBar* bar = nullptr;
    Label* status = nullptr;

    Installer() : Window(T("Instalación de BrysOS"), 640, 440, WF_TITLE | WF_MINIMIZE) {
        icon = &icon_logo;
        back = add(new Button({0, 0, 80, 23}, "", ID_BACK));
        next = add(new Button({0, 0, 80, 23}, "", ID_NEXT));
        cancel = add(new Button({0, 0, 80, 23}, "", ID_CANCEL));
        next->is_default = true;
        int zi = 0;
        for (int k = 0; k < time_zone_count; k++) { if (time_zones[k].offset == settings::utc_offset) zone = zi; zi++; }
        hw_utc = settings::rtc_utc;
        build();
    }

    void on_resize() override {
        int w = client_w(), h = client_h();
        back->r = {w - 3 * 88 - 8, h - 34, 80, 23};
        next->r = {w - 2 * 88 - 8, h - 34, 80, 23};
        cancel->r = {w - 88, h - 34, 80, 23};
    }

    template <typename W> W* page_add(W* w) {
        add(w);
        page_widgets.push(w);
        return w;
    }

    void clear_page() {
        for (Widget* w : page_widgets) {
            widgets.remove(w);
            if (focus == w) focus = nullptr;
            delete w;
        }
        page_widgets.clear();
        list = list2 = nullptr;
        tb1 = tb2 = tb3 = nullptr;
        r1 = r2 = chk = nullptr;
        info = status = nullptr;
        bar = nullptr;
    }

    // ------------------------------------------------------------------ dibujo
    static constexpr int SIDE = 170;

    const char* page_title() {
        switch (page) {
        case P_LANG: return T("Bienvenido a BrysOS 98");
        case P_KEYBOARD: return T("Distribución del teclado");
        case P_MODE: return T("¿Qué desea hacer?");
        case P_TIME: return T("Fecha y hora");
        case P_TARGET: return T("¿Dónde desea instalar BrysOS?");
        case P_BOOT: return T("Gestor de arranque");
        case P_SUMMARY: return T("Listo para instalar");
        case P_PROGRESS: return T("Instalando BrysOS");
        case P_DONE: return T("Instalación completada");
        }
        return "";
    }

    void on_paint(Surface& s) override {
        int w = client_w(), h = client_h();
        s.clear(col::FACE);
        // banda lateral
        for (int y = 0; y < h - 46; y++) s.hline(0, y, SIDE, blend(0x000080, 0x1084D0, y * 256 / (h - 46)));
        draw_icon(s, icon_logo.big, 16, 16);
        s.text(font_large, 50, 20, T("BrysOS 98"), col::WHITE);
        const char* steps[] = {"Idioma", "Teclado", "Fecha y hora", "Ubicación", "Arranque", "Instalar"};
        int cur = page <= P_MODE ? (page == P_LANG ? 0 : 1) : (page == P_TIME ? 2 : page == P_TARGET ? 3 : page == P_BOOT ? 4 : 5);
        for (int i = 0; i < 6; i++) {
            int y = 80 + i * 26;
            const Font& f = i == cur ? font_ui_bold : font_ui;
            uint32_t c = i < cur ? 0xC0D8FF : (i == cur ? col::WHITE : 0x90A8D0);
            if (i < cur) s.text(font_ui_bold, 18, y, T("\xD7"), c); // marca
            else if (i == cur) s.text(font_ui_bold, 18, y, ">", c);
            s.text(f, 34, y, T(steps[i]), c);
        }
        // separador inferior
        s.hline(0, h - 46, w, col::SHADOW);
        s.hline(0, h - 45, w, col::HILIGHT);
        s.text(font_large, SIDE + 20, 16, page_title(), col::BLACK);
    }

    // ------------------------------------------------------------------ páginas
    void build() {
        clear_page();
        int x = SIDE + 20, w = client_w() - SIDE - 40;
        back->text = T("< &Atrás");
        next->text = page == P_SUMMARY ? T("&Instalar") : (page == P_DONE ? T("&Reiniciar") : T("&Siguiente >"));
        cancel->text = page == P_DONE ? T("&Cerrar") : T("Cancelar");
        back->enabled = page > P_LANG && page != P_PROGRESS && page != P_DONE;
        next->enabled = page != P_PROGRESS;
        cancel->enabled = page != P_PROGRESS;
        set_title(T("Instalación de BrysOS"));
        switch (page) {
        case P_LANG: {
            page_add(new Label({x, 56, w, 60}, T("Este asistente instalará BrysOS en su equipo o le permitirá\nprobarlo sin hacer ningún cambio.\n\nElija el idioma:")));
            list = page_add(new ListView({x, 130, 240, 80}, ID_LIST));
            list->mode = ListView::LIST;
            list->add(T("Español"), &icon_logo);
            list->add(T("English"), &icon_logo);
            list->select(i18n::language() == Lang::EN ? 1 : 0);
            focus = list;
            break;
        }
        case P_KEYBOARD: {
            page_add(new Label({x, 56, w, 32}, T("Elija la distribución de su teclado:")));
            list = page_add(new ListView({x, 80, w, 120}, ID_LIST));
            list->mode = ListView::LIST;
            int sel = 0;
            for (int i = 0; i < keyboard::layout_count(); i++) {
                list->add(T(keyboard::layout(i).name), &icon_settings);
                if (!strcmp(keyboard::layout(i).id, keyboard::layout_id())) sel = i;
            }
            list->select(sel);
            page_add(new Label({x, 216, w, 16}, T("Pruebe el teclado aquí (ñ, tildes, @, #...):")));
            tb1 = page_add(new TextBox({x, 236, w, 21}, ""));
            focus = list;
            break;
        }
        case P_MODE: {
            r1 = page_add(new CheckBox({x, 70, w, 20}, T("&Instalar BrysOS en este equipo"), ID_RADIO1, install));
            r1->radio = true;
            page_add(new Label({x + 18, 92, w - 18, 48}, T("Se copiará el sistema a una partición del disco duro y, si lo desea,\nse instalará GRUB para elegir entre BrysOS y los demás sistemas.")));
            r2 = page_add(new CheckBox({x, 150, w, 20}, T("&Probar BrysOS sin instalar"), ID_RADIO2, !install));
            r2->radio = true;
            page_add(new Label({x + 18, 172, w - 18, 48}, T("El sistema funcionará desde la memoria: no se modificará ningún disco\ny los archivos que guarde se perderán al apagar.")));
            focus = r1;
            break;
        }
        case P_TIME: {
            DateTime t = rtc::now();
            if (!date.year) date = t;
            page_add(new Label({x, 60, 120, 16}, T("&Fecha (dd/mm/aaaa):")));
            tb1 = page_add(new TextBox({x + 130, 56, 110, 21}, String::format("%02d/%02d/%04d", date.day, date.month, date.year).c_str()));
            page_add(new Label({x, 90, 120, 16}, T("&Hora (hh:mm):")));
            tb2 = page_add(new TextBox({x + 130, 86, 110, 21}, String::format(T("%02d:%02d"), date.hour, date.minute).c_str()));
            page_add(new Label({x, 122, w, 16}, T("&Zona horaria:")));
            list = page_add(new ListView({x, 140, w, 150}, ID_LIST));
            list->mode = ListView::LIST;
            for (int k = 0; k < time_zone_count; k++) list->add(T(time_zones[k].name), nullptr);
            list->select(zone);
            chk = page_add(new CheckBox({x, 296, w, 20}, T("El reloj del equipo usa la hora &UTC (si también usa Linux)"), ID_CHECK, hw_utc));
            page_add(new Label({x, 320, w, 32}, T("El horario de verano no se cambia automáticamente;\npuede corregir la hora desde el reloj de la barra de tareas.")))->color = col::SHADOW;
            focus = tb1;
            break;
        }
        case P_TARGET: build_target(x, w); break;
        case P_BOOT: build_boot(x, w); break;
        case P_SUMMARY: {
            page_add(new Label({x, 56, w, 280}, summary().c_str()));
            focus = next;
            break;
        }
        case P_PROGRESS: {
            status = page_add(new Label({x, 70, w, 32}, T("Preparando...")));
            bar = page_add(new ProgressBar({x, 110, w, 20}));
            page_add(new Label({x, 150, w, 60}, T("No apague el equipo ni retire el pendrive hasta que termine.")));
            break;
        }
        case P_DONE: {
            String t = T("BrysOS se ha instalado correctamente.\n\n");
            if (!efi_note.empty()) { t += efi_note; t += T("\n\n"); }
            t += T("Retire el pendrive (o la imagen ISO de la máquina virtual) y pulse Reiniciar.\nAl arrancar aparecerá el menú de GRUB para elegir el sistema.");
            if (!grub) t = String(T("BrysOS se ha instalado correctamente.\n\nNo se ha instalado ningún gestor de arranque: tendrá que añadir BrysOS\nal que ya use (multiboot2 /brysos/kernel.elf)."));
            page_add(new Label({x, 56, w, 200}, t.c_str()));
            focus = next;
            break;
        }
        }
        on_resize();
        invalidate();
    }

    // ---- Ubicación
    void scan_disks() {
        disks.clear();
        for (BlockDevice* d : block::disks()) {
            if (d->removable) continue;   // el pendrive de instalación y otros discos USB
            DiskLayout l;
            if (part::read_layout(d, l)) disks.push(move_val(l));
        }
    }

    void build_target(int x, int w) {
        wm::set_busy(true);
        scan_disks();
        wm::set_busy(false);
        page_add(new Label({x, 50, w, 16}, T("Elija una partición, un espacio sin asignar o un disco completo:")));
        list = page_add(new ListView({x, 70, w, 170}, ID_LIST));
        list->mode = ListView::DETAILS;
        list->columns.push({T("Disco / partición"), 190});
        list->columns.push({T("Tamaño"), 70, true});
        list->columns.push({T("Sistema"), 80});
        list->columns.push({T("Etiqueta"), 90});
        rows.clear();
        for (size_t di = 0; di < disks.size(); di++) {
            DiskLayout& l = disks[di];
            String dn = String::format(T("Disco %d: %s"), (int)di + 1, l.disk->model);
            ListItem& it = list->add(dn.c_str(), &icon_hdd);
            it.sub.push(vfs::format_size(l.disk->sectors * 512));
            it.sub.push(String(l.blank ? T("Vacío") : (l.gpt ? "GPT" : "MBR")));
            it.sub.push(String());
            rows.push({TargetRow::DISK, (int)di, -1});
            // particiones y huecos ordenados por posición
            struct Item { uint64_t start; int kind; int idx; };
            Vec<Item> items;
            for (size_t i = 0; i < l.parts.size(); i++) if (l.parts[i].fs != FsType::EXTENDED) items.push({l.parts[i].start, 0, (int)i});
            for (size_t i = 0; i < l.free.size(); i++) items.push({l.free[i].start, 1, (int)i});
            items.sort([](const Item& a, const Item& b) { return a.start < b.start ? -1 : (a.start > b.start ? 1 : 0); });
            for (auto& itm : items) {
                if (itm.kind == 0) {
                    PartInfo& p = l.parts[itm.idx];
                    String n = String::format(T("    Partición %d"), p.number);
                    if (p.esp) n += T(" (sistema EFI)");
                    ListItem& pi = list->add(n.c_str(), nullptr);
                    pi.sub.push(vfs::format_size(p.count * 512));
                    pi.sub.push(String(T(part::fs_name(p.fs))));
                    pi.sub.push(String(p.label));
                    rows.push({TargetRow::PART, (int)di, itm.idx});
                } else {
                    ListItem& fi = list->add(T("    Espacio sin asignar"), nullptr);
                    fi.sub.push(vfs::format_size(l.free[itm.idx].count * 512));
                    fi.sub.push(String());
                    fi.sub.push(String());
                    rows.push({TargetRow::FREE, (int)di, itm.idx});
                }
            }
        }
        if (disks.empty()) {
            page_add(new Label({x, 250, w, 40}, T("No se ha encontrado ningún disco SATA o IDE. Compruebe en la BIOS que\nel controlador SATA está en modo AHCI o IDE.")));
            next->enabled = false;
            return;
        }
        info = page_add(new Label({x, 248, w, 30}, ""));
        r1 = page_add(new CheckBox({x, 280, w / 2, 20}, T("&Formatear (borra los datos)"), ID_RADIO1, !keep_data));
        r1->radio = true;
        r2 = page_add(new CheckBox({x + w / 2, 280, w / 2, 20}, T("&Conservar los archivos"), ID_RADIO2, keep_data));
        r2->radio = true;
        page_add(new Label({x, 312, 120, 16}, T("&Etiqueta del volumen:")));
        tb1 = page_add(new TextBox({x + 130, 308, 100, 21}, label.c_str()));
        page_add(new Label({x + 245, 312, 90, 16}, T("Tamaño (MB):")));
        tb2 = page_add(new TextBox({x + 335, 308, 80, 21}, ""));
        int sel = target_row;
        if (sel < 0 || sel >= (int)rows.size()) {
            // propone el primer espacio libre, o el primer disco vacío
            for (size_t i = 0; i < rows.size(); i++)
                if (rows[i].kind == TargetRow::FREE || (rows[i].kind == TargetRow::DISK && disks[rows[i].disk].blank)) { sel = (int)i; break; }
        }
        if (sel >= 0) list->select(sel);
        target_row = sel;
        update_target();
        focus = list;
    }

    void update_target() {
        if (!info) return;
        r1->visible = r2->visible = false;
        tb2->enabled = false;
        int i = list->selected();
        target_row = i;
        next->enabled = false;
        if (i < 0) { info->set_text(T("Seleccione dónde instalar.")); invalidate(); return; }
        TargetRow& row = rows[i];
        DiskLayout& l = disks[row.disk];
        String msg;
        bool ok = true;
        if (row.kind == TargetRow::DISK) {
            msg = l.blank ? String(T("El disco está vacío: se creará una partición FAT32 que ocupará todo el disco."))
                          : String(T("ATENCIÓN: se borrarán TODAS las particiones y los datos de este disco."));
        } else if (row.kind == TargetRow::FREE) {
            uint64_t mb = l.free[row.index].count / 2048;
            if (!l.gpt && l.free_slots == 0) { msg = T("No se puede crear otra partición primaria: el disco ya tiene cuatro."); ok = false; }
            else if (mb < 64) { msg = T("El espacio libre es demasiado pequeño (mínimo 64 MB)."); ok = false; }
            else {
                msg = T("Se creará una partición FAT32 nueva en el espacio sin asignar. Los demás datos no se tocan.");
                tb2->enabled = true;
                if (tb2->text.empty() || new_size_mb == 0 || new_size_mb > mb) tb2->set_text(String::format(T("%lu"), mb).c_str());
            }
        } else {
            PartInfo& p = l.parts[row.index];
            if (p.esp) { msg = T("Ésta es la partición del sistema EFI: no se puede usar para instalar."); ok = false; }
            else if (p.count < 64 * 2048) { msg = T("La partición es demasiado pequeña (mínimo 64 MB)."); ok = false; }
            else if (p.fs == FsType::FAT32) {
                msg = p.has_brysos ? String(T("Esta partición ya contiene BrysOS: se actualizará."))
                                   : String(T("Partición FAT32: puede formatearla o conservar sus archivos."));
                r1->visible = r2->visible = true;
            } else {
                msg = String::format(T("Se formateará como FAT32 y se BORRARÁN todos sus datos (%s)."), T(part::fs_name(p.fs)));
            }
        }
        info->set_text(msg.c_str());
        info->color = ok ? col::BLACK : col::RED;
        next->enabled = ok;
        invalidate();
    }

    // ---- Arranque
    int target_disk() const { return target_row >= 0 ? rows[target_row].disk : 0; }

    const PartInfo* find_esp(int& disk_out) {
        // primero en el disco de destino, después en cualquiera
        int td = target_disk();
        for (int pass = 0; pass < 2; pass++)
            for (size_t d = 0; d < disks.size(); d++) {
                if (pass == 0 && (int)d != td) continue;
                for (auto& p : disks[d].parts)
                    if (p.esp && p.fs == FsType::FAT32) { disk_out = (int)d; return &p; }
            }
        return nullptr;
    }

    void build_boot(int x, int w) {
        others = bootloader::detect_systems(disks, boot_info.uefi);
        String found;
        if (others.empty()) found = T("No se han encontrado otros sistemas operativos.");
        else {
            found = T("Sistemas encontrados que aparecerán en el menú:\n");
            for (auto& o : others) { found += "     - "; found += o.name; found += T("\n"); }
        }
        page_add(new Label({x, 50, w, 80}, found.c_str()));
        chk = page_add(new CheckBox({x, 136, w, 20}, T("Instalar el gestor de arranque &GRUB (recomendado)"), ID_CHECK, grub));
        if (boot_info.uefi) {
            int ed;
            const PartInfo* esp = find_esp(ed);
            String t = esp ? String::format(T("El equipo arranca en modo UEFI: GRUB se copiará a la partición\ndel sistema EFI (disco %d, partición %d)."), ed + 1, esp->number)
                           : String(T("El equipo arranca en modo UEFI pero no hay ninguna partición del sistema EFI\nen formato FAT32. Créela desde otro sistema para poder instalar GRUB."));
            page_add(new Label({x + 18, 160, w - 18, 40}, t.c_str()));
            if (!esp) { grub = false; chk->checked = false; chk->enabled = false; }
        } else {
            page_add(new Label({x + 18, 160, w - 18, 16}, T("Instalar GRUB en el MBR de:")));
            list = page_add(new ListView({x + 18, 180, w - 18, 80}, ID_LIST));
            list->mode = ListView::LIST;
            for (size_t d = 0; d < disks.size(); d++)
                list->add(String::format(T("Disco %d: %s"), (int)d + 1, disks[d].disk->model).c_str(), &icon_hdd);
            if (grub_disk >= (int)disks.size()) grub_disk = 0;
            grub_disk = target_disk();
            list->select(grub_disk);
            page_add(new Label({x + 18, 264, w - 18, 32}, T("Normalmente es el primer disco (el que la BIOS usa para arrancar).\nSe guardará una copia del MBR original en C:\\BrysOS.")))->color = col::SHADOW;
        }
        page_add(new Label({x, 310, 200, 16}, T("Segundos de espera del menú:")));
        tb1 = page_add(new TextBox({x + 200, 306, 50, 21}, String::format(T("%d"), timeout).c_str()));
        focus = chk;
    }

    String summary() {
        String s;
        s += String::format(T("Idioma: %s\n"), i18n::language() == Lang::EN ? T("English") : T("Español"));
        for (int i = 0; i < keyboard::layout_count(); i++)
            if (!strcmp(keyboard::layout(i).id, keyboard::layout_id())) s += String::format(T("Teclado: %s\n"), T(keyboard::layout(i).name));
        s += String::format(T("Zona horaria: %s\n\n"), T(time_zones[zone].name));
        TargetRow& row = rows[target_row];
        DiskLayout& l = disks[row.disk];
        s += String::format(T("Disco %d: %s\n"), row.disk + 1, l.disk->model);
        if (row.kind == TargetRow::DISK) s += T("  - Borrar el disco completo y crear una partición FAT32\n");
        else if (row.kind == TargetRow::FREE) s += String::format(T("  - Crear una partición FAT32 de %lu MB en el espacio libre\n"), new_size_mb);
        else {
            PartInfo& p = l.parts[row.index];
            if (p.fs == FsType::FAT32 && keep_data) s += String::format(T("  - Instalar en la partición %d conservando sus archivos\n"), p.number);
            else s += String::format(T("  - Formatear la partición %d como FAT32\n"), p.number);
        }
        s += String::format(T("  - Etiqueta del volumen: %s\n\n"), label.c_str());
        if (grub) {
            if (boot_info.uefi) s += T("Gestor de arranque: GRUB en la partición del sistema EFI\n");
            else s += String::format(T("Gestor de arranque: GRUB en el MBR del disco %d\n"), grub_disk + 1);
            s += String::format(T("  - Entradas: BrysOS 98%s\n"), others.empty() ? "" : ", ...");
        } else {
            s += T("Gestor de arranque: no se instalará\n");
        }
        s += T("\nPulse Instalar para empezar. Los cambios en el disco no se pueden deshacer.");
        return s;
    }

    // ------------------------------------------------------------------ validación
    bool leave_page() {
        switch (page) {
        case P_LANG: {
            int i = list->selected();
            strlcpy(settings::language, i == 1 ? T("en") : T("es"), sizeof settings::language);
            i18n::set_language(i == 1 ? Lang::EN : Lang::ES);
            relabel_desktop();
            return true;
        }
        case P_KEYBOARD: {
            int i = list->selected();
            if (i >= 0) {
                keyboard::set_layout(keyboard::layout(i).id);
                strlcpy(settings::keyboard, keyboard::layout(i).id, sizeof settings::keyboard);
            }
            return true;
        }
        case P_MODE: install = r1->checked; return true;
        case P_TIME: {
            int d = 0, m = 0, y = 0, hh = -1, mi = -1;
            if (!parse_date(tb1->text.c_str(), d, m, y) || !parse_time(tb2->text.c_str(), hh, mi)) {
                msgbox(this, T("Fecha y hora"), T("Escriba la fecha como dd/mm/aaaa y la hora como hh:mm."), MB_OK, &icon_warning);
                return false;
            }
            zone = max(0, list->selected());
            hw_utc = chk->checked;
            settings::utc_offset = time_zones[zone].offset;
            settings::rtc_utc = hw_utc;
            rtc::configure(hw_utc, time_zones[zone].offset);
            DateTime t{y, m, d, hh, mi, 0};
            DateTime cur = rtc::now();
            // sólo se reescribe el reloj si el usuario cambió la hora
            if (t.year != cur.year || t.month != cur.month || t.day != cur.day || t.hour != cur.hour || abs_val(t.minute - cur.minute) > 1)
                rtc::set_local(t);
            date = t;
            if (taskbar()) taskbar()->invalidate();
            return true;
        }
        case P_TARGET: {
            if (target_row < 0) return false;
            label = tb1->text;
            for (size_t i = 0; i < label.size(); i++) label[i] = to_upper(label[i]);
            if (label.empty() || label.size() > 11) {
                msgbox(this, T("Ubicación"), T("La etiqueta debe tener entre 1 y 11 caracteres."), MB_OK, &icon_warning);
                return false;
            }
            keep_data = r2->visible && r2->checked;
            TargetRow& row = rows[target_row];
            DiskLayout& l = disks[row.disk];
            if (row.kind == TargetRow::FREE) {
                uint64_t mb = 0;
                for (const char* p = tb2->text.c_str(); *p >= '0' && *p <= '9'; p++) mb = mb * 10 + (uint64_t)(*p - '0');
                uint64_t maxmb = l.free[row.index].count / 2048;
                if (mb < 64 || mb > maxmb) {
                    msgbox(this, T("Ubicación"), String::format(T("El tamaño debe estar entre 64 y %lu MB."), maxmb).c_str(), MB_OK, &icon_warning);
                    return false;
                }
                new_size_mb = mb;
            }
            bool destructive = row.kind == TargetRow::DISK ? !l.blank : (row.kind == TargetRow::PART && !keep_data);
            if (destructive) {
                String q = row.kind == TargetRow::DISK
                               ? String::format(T("Se borrará TODO el contenido del disco %d (%s).\n\n¿Está seguro?"), row.disk + 1, l.disk->model)
                               : String::format(T("Se borrarán todos los datos de la partición %d del disco %d.\n\n¿Está seguro?"),
                                                l.parts[row.index].number, row.disk + 1);
                if (msgbox(this, T("Confirmar"), q.c_str(), MB_YESNO, &icon_warning) != ID_YES) return false;
            }
            return true;
        }
        case P_BOOT: {
            grub = chk->checked;
            if (list) grub_disk = max(0, list->selected());
            int t = 0;
            for (const char* p = tb1->text.c_str(); *p >= '0' && *p <= '9'; p++) t = t * 10 + (*p - '0');
            timeout = clamp(t, 0, 99);
            if (grub && !boot_info.uefi && !bootloader::bios_gap_ok(disks[grub_disk])) {
                msgbox(this, T("Gestor de arranque"),
                       T("No hay espacio para GRUB antes de la primera partición de ese disco (debe empezar en el sector 2048 o después).\n\nDesmarque GRUB o elija otro disco."),
                       MB_OK, &icon_warning);
                return false;
            }
            return true;
        }
        }
        return true;
    }

    static bool parse_num(const char*& p, int& v) {
        if (*p < '0' || *p > '9') return false;
        v = 0;
        while (*p >= '0' && *p <= '9') v = v * 10 + (*p++ - '0');
        return true;
    }
    static bool parse_date(const char* s, int& d, int& m, int& y) {
        if (!parse_num(s, d) || (*s != '/' && *s != '-')) return false;
        s++;
        if (!parse_num(s, m) || (*s != '/' && *s != '-')) return false;
        s++;
        if (!parse_num(s, y)) return false;
        return y >= 2000 && y <= 2099 && m >= 1 && m <= 12 && d >= 1 && d <= rtc::days_in_month(y, m);
    }
    static bool parse_time(const char* s, int& h, int& mi) {
        if (!parse_num(s, h) || (*s != ':' && *s != '.')) return false;
        s++;
        return parse_num(s, mi) && h >= 0 && h < 24 && mi >= 0 && mi < 60;
    }

    void relabel_desktop() {
        if (taskbar()) taskbar()->rebuild();
        if (desktop()) desktop()->rebuild();
    }

    // ------------------------------------------------------------------ instalación
    void progress(int pct, const char* text) {
        if (bar) bar->set(pct);
        if (status) status->set_text(text);
        wm::pump();
    }

    bool fail(const char* what, const char* why) {
        wm::set_busy(false);
        msgbox(this, T("Error de instalación"), String::format(T("%s\n\n%s"), what, why ? why : "").c_str(), MB_OK, &icon_error);
        page = P_TARGET;
        build();
        return false;
    }

    bool do_install() {
        const BootModule* kernel = boot_module(T("kernel.elf"));
        if (!kernel) return fail(T("No se encuentra el núcleo de BrysOS en el medio de instalación."), "");
        wm::set_busy(true);
        TargetRow row = rows[target_row];
        DiskLayout& l = disks[row.disk];
        const char* err = "";
        PartInfo target;
        bool format = true;

        // 1. Partición
        progress(5, T("Preparando la partición..."));
        if (row.kind == TargetRow::DISK) {
            if (!part::whole_disk(l.disk, target, err)) return fail(T("No se pudo crear la tabla de particiones."), err);
        } else if (row.kind == TargetRow::FREE) {
            if (!part::create(l, l.free[row.index].start, new_size_mb * 2048, target, err))
                return fail(T("No se pudo crear la partición."), err);
        } else {
            target = l.parts[row.index];
            if (target.fs == FsType::FAT32 && keep_data) format = false;
            else if (!part::set_fat32_type(l, target, err)) return fail(T("No se pudo cambiar el tipo de partición."), err);
        }
        // 2. Formato
        Partition* dev = part::open(l.disk, target);
        if (format) {
            progress(15, T("Dando formato FAT32 a la partición..."));
            if (!Fat32::format(dev, label.c_str())) { delete dev; return fail(T("No se pudo dar formato a la partición."), T("Compruebe que tiene al menos 64 MB.")); }
        }
        char letter = vfs::mount_rw(dev);
        if (!letter) { delete dev; return fail(T("No se puede montar la partición de destino."), ""); }
        Volume* v = vfs::volume(letter);
        if (!format) v->fs->set_label(label.c_str());
        char uuid[16];
        ksnprintf(uuid, sizeof uuid, "%04X-%04X", v->fs->serial >> 16, v->fs->serial & 0xFFFF);
        String root = String::format("%c:\\", letter);
        auto path = [&](const char* rel) { return root + rel; };

        // 3. Archivos del sistema
        progress(30, T("Copiando el núcleo de BrysOS..."));
        vfs::mkdir(path(sys::REL_DIR).c_str());
        if (!vfs::write_file(path(sys::REL_KERNEL).c_str(), kernel->data(), (uint32_t)kernel->size)) {
            vfs::unmount(letter);
            return fail(T("No se pudo copiar el núcleo."), vfs::error());
        }
        // bibliotecas del sistema (.bsl): las que trae el medio de instalación
        progress(38, T("Copiando las bibliotecas del sistema..."));
        Vec<String> bsl_files;
        vfs::mkdir(path(sys::REL_BSL_DIR).c_str());
        for (int i = 0; i < boot_info.module_count; i++) {
            const BootModule& m = boot_info.modules[i];
            size_t n = strlen(m.name);
            if (n < 5 || stricmp(m.name + n - 4, ".bsl")) continue;
            String dst = vfs::join(path(sys::REL_BSL_DIR).c_str(), m.name);
            if (!vfs::write_file(dst.c_str(), m.data(), (uint32_t)m.size)) {
                vfs::unmount(letter);
                return fail(T("No se pudieron copiar las bibliotecas del sistema."), vfs::error());
            }
            bsl_files.push(String(m.name));
        }
        progress(45, T("Guardando la configuración..."));
        char cfg[768];
        settings::serialize(cfg, sizeof cfg);
        vfs::write_file(path(sys::REL_CONFIG).c_str(), cfg, (uint32_t)strlen(cfg));
        vfs::write_file(path(sys::REL_VERSION).c_str(), BRYSOS_VERSION "\r\n", (uint32_t)strlen(BRYSOS_VERSION) + 2);
        vfs::mkdir(path(T("Mis documentos")).c_str());
        vfs::mkdir((path(T("Mis documentos")) + "\\" + T("Mis imágenes")).c_str());
        vfs::mkdir(path(T("Papelera")).c_str());

        // 4. GRUB
        if (grub) {
            progress(60, T("Instalando el gestor de arranque GRUB..."));
            // primer arranque: idioma elegido y presentación con logo y sonido
            String args = String::format("lang=%s vol=%d introvol=%d intro", settings::language, settings::volume, settings::intro_volume);
            String cfgtext = bootloader::grub_cfg(uuid, args.c_str(), others, timeout, bsl_files);
            // GRUB lee el archivo en UTF-8
            String utf;
            for (size_t i = 0; i < cfgtext.size(); i++) {
                uint8_t c = (uint8_t)cfgtext[i];
                if (c < 0x80) utf.append((char)c);
                else { utf.append((char)(0xC0 | (c >> 6))); utf.append((char)(0x80 | (c & 0x3F))); }
            }
            if (!vfs::write_file(path(sys::REL_GRUB_CFG).c_str(), utf.c_str(), (uint32_t)utf.size())) {
                vfs::unmount(letter);
                return fail(T("No se pudo escribir grub.cfg."), vfs::error());
            }
            efi_note.clear();
            if (boot_info.uefi) {
                int ed;
                const PartInfo* esp = find_esp(ed);
                bool ok = esp && bootloader::install_efi(disks[ed], *esp, efi_note, err);
                if (!ok) { vfs::unmount(letter); return fail(T("No se pudo instalar GRUB."), err); }
            } else {
                Vec<uint8_t> backup;
                DiskLayout& gl = disks[grub_disk];
                // la tabla puede haber cambiado: se vuelve a leer
                DiskLayout fresh;
                if (part::read_layout(gl.disk, fresh)) gl = move_val(fresh);
                if (!bootloader::install_bios(gl, backup, err)) { vfs::unmount(letter); return fail(T("No se pudo instalar GRUB."), err); }
                vfs::write_file(path(sys::REL_MBR_BACKUP).c_str(), backup.data(), (uint32_t)backup.size());
            }
        }
        progress(90, T("Terminando..."));
        vfs::unmount(letter);
        for (BlockDevice* d : block::disks()) d->flush();
        progress(100, T("Instalación completada."));
        wm::set_busy(false);
        return true;
    }

    // ------------------------------------------------------------------ eventos
    void on_command(int id, int code) override {
        switch (id) {
        case ID_NEXT:
            if (page == P_DONE) {
                for (Volume* v : vfs::volumes()) if (!v->read_only) v->fs->sync();
                acpi::reboot();
            }
            if (!leave_page()) return;
            if (page == P_MODE && !install) { finish_trial(); return; }
            if (page == P_SUMMARY) {
                page = P_PROGRESS;
                build();
                if (do_install()) { page = P_DONE; build(); }
                return;
            }
            page++;
            build();
            return;
        case ID_BACK:
            if (page > P_LANG) { page--; build(); }
            return;
        case ID_CANCEL:
            close();
            return;
        case ID_LIST:
            if (page == P_TARGET && code == LV_SELCHANGE) update_target();
            if (page == P_LANG && code == LV_SELCHANGE && list->selected() >= 0) {
                i18n::set_language(list->selected() == 1 ? Lang::EN : Lang::ES);
                strlcpy(settings::language, i18n::code(), sizeof settings::language);
                int sel = list->selected();
                relabel_desktop();
                build();
                if (list) list->select(sel);
            }
            if (page == P_KEYBOARD && code == LV_SELCHANGE && list->selected() >= 0)
                keyboard::set_layout(keyboard::layout(list->selected()).id);
            if (code == LV_ACTIVATE) on_command(ID_NEXT, 0);
            return;
        case ID_RADIO1:
            if (r2) { r1->checked = true; r2->checked = false; invalidate(); }
            return;
        case ID_RADIO2:
            if (r1) { r1->checked = false; r2->checked = true; invalidate(); }
            return;
        }
    }

    void finish_trial() {
        strlcpy(settings::keyboard, keyboard::layout_id(), sizeof settings::keyboard);
        wm::destroy(this);
    }

    bool on_close() override {
        if (page == P_PROGRESS) return false;
        if (page == P_DONE) return true;
        return msgbox(this, T("Instalación de BrysOS"), T("¿Desea salir del programa de instalación?\nPodrá volver a abrirlo desde el icono \"Instalar BrysOS\" del escritorio."),
                      MB_YESNO, &icon_question) == ID_YES;
    }

    bool on_key(const InputEvent& e) override {
        if (e.pressed && e.key == KEY_ENTER && next->enabled) {
            on_command(ID_NEXT, 0);
            return true;
        }
        return false;
    }
};

}

namespace apps {
void installer() {
    if (!block::fixed_count()) {
        msgbox(nullptr, T("Instalación de BrysOS"),
               T("No se detectó ningún disco duro al arrancar, por lo que no se puede continuar con la instalación.\n\n"
                 "Puede seguir usando BrysOS sin instalar. En el Símbolo del sistema, la orden REGISTRO\n"
                 "muestra lo que ocurrió al buscar el disco."),
               MB_OK, &icon_error);
        return;
    }
    for (Window* w : wm::windows())
        if (w->icon == &icon_logo && !strcmp(w->title, T("Instalación de BrysOS"))) { wm::activate(w); return; }
    auto* w = new Installer();
    w->center();
    wm::add(w);
}
}

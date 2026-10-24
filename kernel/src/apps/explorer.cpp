// Explorador de BrysOS: navegación por las unidades y gestión de archivos
#include "apps/common.h"
#include "fs/fat32.h"

namespace {
enum {
    ID_LIST = 10, ID_ADDR, ID_BACK, ID_FWD, ID_UP, ID_CUT, ID_COPY, ID_PASTE, ID_DELETE, ID_PROPS, ID_VIEWS,
    M_NEW_FOLDER = 100, M_NEW_TEXT, M_OPEN, M_DELETE, M_RENAME, M_PROPS, M_EMPTY_TRASH, M_CLOSE,
    M_CUT, M_COPY, M_PASTE, M_SELALL, M_INVERT,
    M_VIEW_ICONS, M_VIEW_LIST, M_VIEW_DETAILS, M_SORT_NAME, M_SORT_TYPE, M_SORT_SIZE, M_SORT_DATE, M_REFRESH,
    M_BACK, M_FWD, M_UP, M_GO_MYPC, M_GO_DOCS, M_GO_C, M_ABOUT, M_RESTORE, M_EJECT,
};

constexpr int TOOLBAR_H = 42, ADDR_H = 26, STATUS_H = 20;

bool in_trash(const String& p) {
    String t = vfs::normalize(vfs::trash_dir().c_str());
    return p.size() >= t.size() && !strncmp(p.c_str(), t.c_str(), t.size()) &&
           (p.size() == t.size() || p[t.size()] == '\\');
}

class Explorer : public Window {
public:
    String path;               // "" = Mi PC
    Vec<String> back, fwd;
    ListView* list;
    TextBox* addr;
    Label* status;
    Label* status2;
    Vec<Button*> tools;
    Menu ctx_item, ctx_bg, ctx_drive;
    int sort_col = 0;
    bool sort_desc = false;
    Vec<VfsEntry> entries;
    uint32_t drives_gen = 0;

    Explorer(const char* start) : Window(T("Explorador"), 620, 400, WF_NORMAL) {
        icon = &icon_folder_open;
        min_w = 360;
        min_h = 220;
        auto* mb = new MenuBar();
        Menu* archivo = mb->add(T("&Archivo"));
        Menu* nuevo = new Menu();
        nuevo->add(T("&Carpeta"), M_NEW_FOLDER, nullptr, &icon_folder).add(T("&Documento de texto"), M_NEW_TEXT, nullptr, &icon_file_text);
        archivo->submenu(T("&Nuevo"), nuevo).separator()
            .add(T("&Abrir"), M_OPEN).add(T("&Eliminar"), M_DELETE, T("Supr")).add(T("Cambiar &nombre"), M_RENAME, "F2")
            .add(T("&Propiedades"), M_PROPS).add(T("E&xpulsar"), M_EJECT).separator().add(T("&Vaciar Papelera de reciclaje"), M_EMPTY_TRASH)
            .separator().add(T("&Cerrar"), M_CLOSE);
        mb->add(T("&Edición"))->add(T("Cor&tar"), M_CUT, T("Ctrl+X")).add(T("&Copiar"), M_COPY, T("Ctrl+C")).add(T("&Pegar"), M_PASTE, T("Ctrl+V"))
            .separator().add(T("Seleccionar &todo"), M_SELALL, T("Ctrl+E")).add(T("&Invertir selección"), M_INVERT);
        Menu* ver = mb->add(T("&Ver"));
        Menu* org = new Menu();
        org->add(T("Por &nombre"), M_SORT_NAME).add(T("Por &tipo"), M_SORT_TYPE).add(T("Por ta&maño"), M_SORT_SIZE).add(T("Por &fecha"), M_SORT_DATE);
        ver->add(T("Iconos &grandes"), M_VIEW_ICONS).add(T("&Lista"), M_VIEW_LIST).add(T("&Detalles"), M_VIEW_DETAILS)
            .separator().submenu(T("Organizar &iconos"), org).separator().add(T("&Actualizar"), M_REFRESH, "F5");
        mb->add(T("&Ir"))->add(T("&Atrás"), M_BACK, T("Retroceso")).add(T("A&delante"), M_FWD).add(T("&Subir un nivel"), M_UP)
            .separator().add(T("&Mi PC"), M_GO_MYPC, nullptr, &icon_computer).add(T("Mis &documentos"), M_GO_DOCS, nullptr, &icon_mydocs)
            .add(T("Unidad &C:"), M_GO_C, nullptr, &icon_hdd);
        mb->add(T("Ay&uda"))->add(T("&Acerca de BrysOS"), M_ABOUT);
        attach_menubar(this, mb);

        struct ToolDef { const char* t; const IconPair* ic; int id; };
        const ToolDef tb[] = {{"Atrás", &icon_arrow_left, ID_BACK}, {"Adelante", &icon_arrow_right, ID_FWD},
                        {"Subir", &icon_up_folder, ID_UP}, {nullptr, nullptr, 0},
                        {"Cortar", &icon_cut, ID_CUT}, {"Copiar", &icon_copy, ID_COPY}, {"Pegar", &icon_paste, ID_PASTE},
                        {nullptr, nullptr, 0}, {"Eliminar", &icon_delete, ID_DELETE},
                        {"Propiedades", &icon_props, ID_PROPS}, {"Vistas", &icon_views, ID_VIEWS}};
        int x = 4;
        for (auto& t : tb) {
            if (!t.t) { x += 8; continue; }
            const char* label = T(t.t);
            int w = max(48, font_ui.text_width(label) + 12);
            Button* b = add(new Button({x, 2, w, 38}, label, t.id));
            b->icon = &t.ic->small;
            b->flat = true;
            b->focusable = false;
            tools.push(b);
            x += w + 1;
        }
        add(new Label({6, TOOLBAR_H + 6, 60, 16}, T("Dirección")));
        addr = add(new TextBox({62, TOOLBAR_H + 2, 300, 21}, "", ID_ADDR));
        list = add(new ListView({0, 0, 10, 10}, ID_LIST));
        status = add(new Label({0, 0, 10, 10}, ""));
        status->sunken = true;
        status2 = add(new Label({0, 0, 10, 10}, ""));
        status2->sunken = true;
        ctx_drive.add(T("&Abrir"), M_OPEN).separator().add(T("E&xpulsar"), M_EJECT).separator().add(T("&Propiedades"), M_PROPS);
        ctx_item.add(T("&Abrir"), M_OPEN).separator().add(T("Cor&tar"), M_CUT).add(T("&Copiar"), M_COPY).separator()
            .add(T("&Eliminar"), M_DELETE).add(T("Cambiar &nombre"), M_RENAME).separator().add(T("&Propiedades"), M_PROPS);
        Menu* nuevo2 = new Menu();
        nuevo2->add(T("&Carpeta"), M_NEW_FOLDER, nullptr, &icon_folder).add(T("&Documento de texto"), M_NEW_TEXT, nullptr, &icon_file_text);
        Menu* vista = new Menu();
        vista->add(T("Iconos &grandes"), M_VIEW_ICONS).add(T("&Lista"), M_VIEW_LIST).add(T("&Detalles"), M_VIEW_DETAILS);
        ctx_bg.submenu(T("&Ver"), vista).separator().add(T("&Pegar"), M_PASTE).add(T("&Actualizar"), M_REFRESH).separator()
            .submenu(T("&Nuevo"), nuevo2).separator().add(T("&Propiedades"), M_PROPS);
        focus = list;
        navigate(start, false);
        drives_gen = vfs::generation();
        wm::set_timer(this, 1, 1000);
    }

    // Cambio de idioma: además de los controles, la lista (tipos, fechas, nombres de unidad)
    void retranslate() override {
        Window::retranslate();
        refresh();
    }

    // Unidades USB conectadas o retiradas
    void on_timer(int) override {
        if (drives_gen == vfs::generation()) return;
        drives_gen = vfs::generation();
        if (is_mypc()) refresh();
        else if (!vfs::volume(path[0])) navigate(nullptr, false);
    }

    void on_resize() override {
        int w = client_w(), h = client_h();
        addr->r = {62, TOOLBAR_H + 2, w - 66, 21};
        int top = TOOLBAR_H + ADDR_H;
        list->r = {0, top, w, h - top - STATUS_H - 1};
        int s1 = w * 2 / 3;
        status->r = {0, h - STATUS_H, s1 - 2, STATUS_H};
        status2->r = {s1, h - STATUS_H, w - s1, STATUS_H};
    }

    void on_paint(Surface& s) override {
        s.clear(col::FACE);
        int w = client_w();
        // separadores de las bandas de herramientas
        s.hline(0, 0, w, col::SHADOW);
        s.hline(0, 1, w, col::HILIGHT);
        s.bevel_thin_raised({0, TOOLBAR_H, w, ADDR_H});
    }

    bool is_mypc() const { return path.empty(); }

    void navigate(const char* p, bool record = true) {
        String np = (p && p[0]) ? vfs::normalize(p) : String();
        if (!np.empty() && !vfs::is_dir(np.c_str())) {
            msgbox(this, T("Explorador"), String::format(T("No se encuentra '%s'. Compruebe la ortografía e inténtelo de nuevo."), p).c_str(), MB_OK, &icon_error);
            addr->set_text(path.empty() ? T("Mi PC") : path.c_str());
            return;
        }
        if (record && np != path.c_str()) {
            back.push(path);
            fwd.clear();
        }
        path = np;
        refresh();
    }

    void refresh() {
        list->clear();
        entries.clear();
        if (is_mypc()) {
            set_title(T("Mi PC"));
            icon = &icon_computer;
            addr->set_text(T("Mi PC"));
            list->columns.clear();
            list->columns.push({T("Nombre"), 180});
            list->columns.push({T("Tipo"), 130});
            list->columns.push({T("Tamaño total"), 100, true});
            list->columns.push({T("Espacio libre"), 100, true});
            for (Volume* v : vfs::volumes()) {
                String name = String::format(T("%s (%c:)"), v->label.empty() ? (v->removable ? T("Disco extraíble") : T("Disco local")) : v->label.c_str(), v->letter);
                ListItem& it = list->add(name.c_str(), v->removable ? &icon_usbdrive : &icon_hdd, v->letter);
                it.path = String::format("%c:\\", v->letter);
                it.sub.push(String(v->ram ? T("Disco en memoria") : v->removable ? T("Disco extraíble USB")
                                   : (v->read_only ? T("Disco local (sólo lectura)") : T("Disco local"))));
                it.sub.push(vfs::format_size(v->fs->total_bytes()));
                it.sub.push(vfs::format_size(v->fs->free_bytes()));
            }
        } else {
            String b = vfs::basename(path.c_str());
            if (path.size() <= 3) {
                Volume* v = vfs::volume(path[0]);
                b = String::format(T("%s (%c:)"), v && !v->label.empty() ? v->label.c_str() : T("Disco local"), path[0]);
            }
            set_title(b.c_str());
            icon = in_trash(path) ? &icon_recycle : &icon_folder_open;
            addr->set_text(path.c_str());
            list->columns.clear();
            list->columns.push({T("Nombre"), 200});
            list->columns.push({T("Tamaño"), 80, true});
            list->columns.push({T("Tipo"), 150});
            list->columns.push({T("Modificado"), 120});
            if (!vfs::list(path.c_str(), entries)) {
                msgbox(this, T("Explorador"), vfs::error(), MB_OK, &icon_error);
            }
            sort_entries();
            for (size_t i = 0; i < entries.size(); i++) {
                VfsEntry& e = entries[i];
                if (e.attr & FAT_HIDDEN) continue;
                ListItem& it = list->add(e.name.c_str(), icon_for(e.name.c_str(), e.dir), (int64_t)i);
                it.path = vfs::join(path.c_str(), e.name.c_str());
                it.sub.push(e.dir ? String() : vfs::format_size(e.size));
                it.sub.push(type_name(e.name.c_str(), e.dir));
                it.sub.push(format_date(e.mtime));
            }
        }
        update_status();
        invalidate();
    }

    void sort_entries() {
        int c = sort_col;
        bool d = sort_desc;
        entries.sort([c, d](const VfsEntry& a, const VfsEntry& b) {
            if (a.dir != b.dir) return a.dir ? -1 : 1;
            int r = 0;
            if (c == 1) r = a.size < b.size ? -1 : (a.size > b.size ? 1 : 0);
            else if (c == 2) r = stricmp(vfs::extension(a.name.c_str()).c_str(), vfs::extension(b.name.c_str()).c_str());
            else if (c == 3) {
                auto key = [](const DateTime& t) { return (((((int64_t)t.year * 13 + t.month) * 32 + t.day) * 24 + t.hour) * 60 + t.minute) * 60 + t.second; };
                int64_t ka = key(a.mtime), kb = key(b.mtime);
                r = ka < kb ? -1 : (ka > kb ? 1 : 0);
            }
            if (r == 0) r = stricmp(a.name.c_str(), b.name.c_str());
            return d ? -r : r;
        });
    }

    void update_status() {
        int sel = list->selected_count();
        uint64_t bytes = 0;
        for (auto& it : list->items) {
            if (is_mypc() || (sel && !it.selected)) continue;
            bytes += entries[(size_t)it.data].size;
        }
        if (sel) status->set_text(String::format(T("%d objeto(s) seleccionado(s)"), sel).c_str());
        else status->set_text(String::format(T("%d objeto(s)"), (int)list->items.size()).c_str());
        if (is_mypc()) status2->set_text(T("Mi PC"));
        else {
            Volume* v = vfs::volume(path[0]);
            String s = vfs::format_size(bytes);
            if (v) s += String::format(T("   (Espacio libre: %s)"), vfs::format_size(v->fs->free_bytes()).c_str());
            status2->set_text(s.c_str());
        }
        tools[1]->enabled = !fwd.empty();
        tools[0]->enabled = !back.empty();
    }

    // Letra de la unidad extraíble seleccionada en Mi PC (o de la carpeta actual), o 0
    char selected_removable() {
        char l = 0;
        if (is_mypc()) {
            int i = list->selected();
            if (i >= 0) l = (char)list->items[i].data;
        } else {
            l = path[0];
        }
        Volume* v = l ? vfs::volume(l) : nullptr;
        return v && v->removable ? l : 0;
    }

    void do_eject() {
        char l = selected_removable();
        if (!l) return;
        String model;
        wm::set_busy(true);
        bool ok = vfs::eject(l, model);
        wm::set_busy(false);
        if (!ok) { msgbox(this, T("Expulsar"), vfs::error(), MB_OK, &icon_error); return; }
        drives_gen = vfs::generation();
        if (!is_mypc()) navigate("");
        else refresh();
        msgbox(this, T("Quitar hardware con seguridad"),
               String::format(T("Ya puede quitar el dispositivo \"%s\" con seguridad."), model.c_str()).c_str(), MB_OK, &icon_usbdrive);
    }

    Vec<String> selection() {
        Vec<String> out;
        for (auto& it : list->items) if (it.selected) out.push(it.path);
        return out;
    }

    void open_item(int i) {
        if (i < 0) return;
        const String& p = list->items[i].path;
        if (is_mypc() || vfs::is_dir(p.c_str())) navigate(p.c_str());
        else apps::open_file(p.c_str());
    }

    void do_delete() {
        Vec<String> sel = selection();
        if (sel.empty() || is_mypc()) return;
        bool to_trash = to_upper(path[0]) == 'C' && !in_trash(path) && vfs::volume('C') && !vfs::volume('C')->read_only;
        String q;
        if (sel.size() == 1) {
            String n = vfs::basename(sel[0].c_str());
            q = to_trash ? String::format(T("¿Está seguro de que desea enviar '%s' a la Papelera de reciclaje?"), n.c_str())
                         : String::format(T("¿Está seguro de que desea eliminar '%s' de forma permanente?"), n.c_str());
        } else {
            q = to_trash ? String::format(T("¿Está seguro de que desea enviar estos %d elementos a la Papelera de reciclaje?"), (int)sel.size())
                         : String::format(T("¿Está seguro de que desea eliminar estos %d elementos de forma permanente?"), (int)sel.size());
        }
        if (msgbox(this, sel.size() == 1 ? T("Confirmar la eliminación del archivo") : T("Confirmar la eliminación de varios archivos"),
                   q.c_str(), MB_YESNO, to_trash ? &icon_recycle : &icon_warning) != ID_YES)
            return;
        wm::set_busy(true);
        for (auto& p : sel) {
            bool ok;
            if (to_trash) {
                String b = vfs::basename(p.c_str());
                int dot = b.rfind('.');
                String base = dot > 0 ? b.substr(0, (size_t)dot) : b;
                String ext = dot > 0 ? b.substr((size_t)dot) : String();
                String dest = vfs::unique_name(vfs::trash_dir().c_str(), base.c_str(), ext.c_str());
                ok = vfs::rename(p.c_str(), dest.c_str());
            } else {
                ok = vfs::remove_tree(p.c_str());
            }
            if (!ok) {
                wm::set_busy(false);
                msgbox(this, T("Error al eliminar"), String::format(T("No se puede eliminar %s: %s"), vfs::basename(p.c_str()).c_str(), vfs::error()).c_str(), MB_OK, &icon_error);
                break;
            }
        }
        wm::set_busy(false);
        refresh();
    }

    void do_rename() {
        int i = list->selected();
        if (i < 0 || is_mypc()) return;
        String p = list->items[i].path;
        String name = vfs::basename(p.c_str());
        if (!input_box(this, T("Cambiar nombre"), T("Escriba el nuevo nombre:"), name)) return;
        if (name.empty() || name == vfs::basename(p.c_str())) return;
        if (!vfs::rename(p.c_str(), vfs::join(path.c_str(), name.c_str()).c_str()))
            msgbox(this, T("Error al cambiar el nombre"), vfs::error(), MB_OK, &icon_error);
        refresh();
        for (size_t k = 0; k < list->items.size(); k++)
            if (!stricmp(list->items[k].text.c_str(), name.c_str())) list->select((int)k);
    }

    void do_clip(bool cut) {
        Vec<String> sel = selection();
        if (sel.empty() || is_mypc()) return;
        fileclip::paths = sel;
        fileclip::cut = cut;
    }

    void do_paste() {
        if (is_mypc() || fileclip::paths.empty()) return;
        wm::set_busy(true);
        for (auto& src : fileclip::paths) {
            String name = vfs::basename(src.c_str());
            String dest = vfs::join(path.c_str(), name.c_str());
            bool ok;
            if (!stricmp(vfs::parent(src.c_str()).c_str(), path.c_str())) {
                if (fileclip::cut) continue;
                dest = vfs::join(path.c_str(), (String(T("Copia de ")) + name).c_str());
                for (int n = 2; vfs::exists(dest.c_str()) && n < 100; n++)
                    dest = vfs::join(path.c_str(), String::format(T("Copia (%d) de %s"), n, name.c_str()).c_str());
            }
            if (vfs::exists(dest.c_str())) {
                wm::set_busy(false);
                String q = String::format(T("Esta carpeta ya contiene un elemento llamado '%s'.\n¿Desea reemplazarlo?"), name.c_str());
                if (msgbox(this, T("Confirmar el reemplazo"), q.c_str(), MB_YESNO, &icon_warning) != ID_YES) continue;
                wm::set_busy(true);
                vfs::remove_tree(dest.c_str());
            }
            ok = fileclip::cut ? vfs::rename(src.c_str(), dest.c_str()) : vfs::copy(src.c_str(), dest.c_str());
            if (!ok) {
                wm::set_busy(false);
                msgbox(this, fileclip::cut ? T("Error al mover") : T("Error al copiar"),
                       String::format(T("No se puede %s %s: %s"), fileclip::cut ? T("mover") : T("copiar"), name.c_str(), vfs::error()).c_str(),
                       MB_OK, &icon_error);
                break;
            }
        }
        if (fileclip::cut) fileclip::paths.clear();
        wm::set_busy(false);
        refresh();
    }

    void new_item(bool folder) {
        if (is_mypc()) return;
        String p = folder ? vfs::unique_name(path.c_str(), T("Nueva carpeta"), "")
                          : vfs::unique_name(path.c_str(), T("Nuevo documento de texto"), T(".txt"));
        bool ok = folder ? vfs::mkdir(p.c_str()) : vfs::write_file(p.c_str(), "", 0);
        if (!ok) { msgbox(this, T("Explorador"), vfs::error(), MB_OK, &icon_error); return; }
        refresh();
        for (size_t k = 0; k < list->items.size(); k++)
            if (!stricmp(list->items[k].path.c_str(), p.c_str())) list->select((int)k);
        do_rename();
    }

    void empty_trash() {
        Vec<VfsEntry> ents;
        String trash = vfs::trash_dir();
        if (!vfs::list(trash.c_str(), ents) || ents.empty()) { msgbox(this, T("Papelera de reciclaje"), T("La Papelera de reciclaje está vacía."), MB_OK, &icon_recycle); return; }
        String q = String::format(T("¿Está seguro de que desea eliminar los %d elementos de la Papelera de reciclaje?"), (int)ents.size());
        if (msgbox(this, T("Confirmar la eliminación de varios archivos"), q.c_str(), MB_YESNO, &icon_recycle) != ID_YES) return;
        wm::set_busy(true);
        for (auto& e : ents) vfs::remove_tree(vfs::join(trash.c_str(), e.name.c_str()).c_str());
        wm::set_busy(false);
        refresh();
    }

    void set_view(ListView::Mode m) {
        list->mode = m;
        list->scroll = 0;
        list->invalidate();
    }

    void on_menu_init(Menu* m) override {
        bool sel = list->selected() >= 0;
        m->enable(M_OPEN, sel);
        m->enable(M_DELETE, sel && !is_mypc());
        m->enable(M_RENAME, sel && !is_mypc());
        m->enable(M_CUT, sel && !is_mypc());
        m->enable(M_COPY, sel && !is_mypc());
        m->enable(M_PASTE, !fileclip::paths.empty() && !is_mypc());
        m->enable(M_NEW_FOLDER, !is_mypc());
        m->enable(M_NEW_TEXT, !is_mypc());
        m->enable(M_EMPTY_TRASH, in_trash(path));
        m->enable(M_EJECT, selected_removable() != 0);
        m->enable(M_BACK, !back.empty());
        m->enable(M_FWD, !fwd.empty());
        m->enable(M_UP, !is_mypc());
        m->check(M_VIEW_ICONS, list->mode == ListView::ICONS);
        m->check(M_VIEW_LIST, list->mode == ListView::LIST);
        m->check(M_VIEW_DETAILS, list->mode == ListView::DETAILS);
    }

    void go_back() {
        if (back.empty()) return;
        fwd.push(path);
        String p = back.back();
        back.pop();
        navigate(p.c_str(), false);
    }

    void go_fwd() {
        if (fwd.empty()) return;
        back.push(path);
        String p = fwd.back();
        fwd.pop();
        navigate(p.c_str(), false);
    }

    void go_up() {
        if (is_mypc()) return;
        if (path.size() <= 3) navigate("");
        else navigate(vfs::parent(path.c_str()).c_str());
    }

    void sort_by(int c) {
        if (sort_col == c) sort_desc = !sort_desc;
        else { sort_col = c; sort_desc = false; }
        refresh();
    }

    void on_command(int id, int code) override {
        switch (id) {
        case ID_LIST:
            if (code == LV_ACTIVATE) open_item(list->selected());
            else if (code == LV_SELCHANGE) update_status();
            else if (code == LV_CONTEXT) {
                on_menu_init(&ctx_item);
                on_menu_init(&ctx_bg);
                on_menu_init(&ctx_drive);
                Menu* m = list->selected() < 0 ? &ctx_bg : (is_mypc() ? &ctx_drive : &ctx_item);
                show_menu(m, this, list->ctx_x, list->ctx_y);
            } else if (code == LV_DELETE) do_delete();
            else if (code == LV_RENAME) do_rename();
            else if (code == LV_BACK) go_up();
            else if (code == LV_COLUMN && !is_mypc()) sort_by(list->clicked_column);
            return;
        case ID_ADDR:
            if (code == 1) {
                String t = addr->text;
                if (!stricmp(t.c_str(), T("Mi PC")) || t.empty()) navigate("");
                else if (vfs::exists(t.c_str()) && !vfs::is_dir(t.c_str())) apps::open_file(vfs::normalize(t.c_str()).c_str());
                else navigate(t.c_str());
                list->focus();
            }
            return;
        case ID_BACK: case M_BACK: go_back(); return;
        case ID_FWD: case M_FWD: go_fwd(); return;
        case ID_UP: case M_UP: go_up(); return;
        case ID_CUT: case M_CUT: do_clip(true); return;
        case ID_COPY: case M_COPY: do_clip(false); return;
        case ID_PASTE: case M_PASTE: do_paste(); return;
        case ID_DELETE: case M_DELETE: do_delete(); return;
        case ID_PROPS: case M_PROPS: {
            int i = list->selected();
            if (i >= 0) apps::properties(list->items[i].path.c_str());
            else if (!is_mypc()) apps::properties(path.c_str());
            return;
        }
        case ID_VIEWS:
            set_view(list->mode == ListView::ICONS ? ListView::LIST : (list->mode == ListView::LIST ? ListView::DETAILS : ListView::ICONS));
            return;
        case M_NEW_FOLDER: new_item(true); return;
        case M_NEW_TEXT: new_item(false); return;
        case M_OPEN: open_item(list->selected()); return;
        case M_RENAME: do_rename(); return;
        case M_EMPTY_TRASH: empty_trash(); return;
        case M_EJECT: do_eject(); return;
        case M_CLOSE: close(); return;
        case M_SELALL:
            for (auto& it : list->items) it.selected = true;
            list->invalidate();
            update_status();
            return;
        case M_INVERT:
            for (auto& it : list->items) it.selected = !it.selected;
            list->invalidate();
            update_status();
            return;
        case M_VIEW_ICONS: set_view(ListView::ICONS); return;
        case M_VIEW_LIST: set_view(ListView::LIST); return;
        case M_VIEW_DETAILS: set_view(ListView::DETAILS); return;
        case M_SORT_NAME: sort_col = 0; sort_desc = false; refresh(); return;
        case M_SORT_SIZE: sort_col = 1; sort_desc = false; refresh(); return;
        case M_SORT_TYPE: sort_col = 2; sort_desc = false; refresh(); return;
        case M_SORT_DATE: sort_col = 3; sort_desc = false; refresh(); return;
        case M_REFRESH: refresh(); return;
        case M_GO_MYPC: navigate(""); return;
        case M_GO_DOCS: navigate(vfs::docs_dir().c_str()); return;
        case M_GO_C: navigate("C:\\"); return;
        case M_ABOUT:
            msgbox(this, T("Acerca de BrysOS"), T("BrysOS 98\nSistema operativo de 64 bits escrito desde cero en C++.\n\nExplorador de BrysOS, versión 1.0"), MB_OK, &icon_logo);
            return;
        }
    }

    bool on_key(const InputEvent& e) override {
        if (!e.pressed) return false;
        if (e.key == KEY_F5) { refresh(); return true; }
        if (e.mods & MOD_CTRL) {
            if (e.key == 'x') { do_clip(true); return true; }
            if (e.key == 'c') { do_clip(false); return true; }
            if (e.key == 'v') { do_paste(); return true; }
            if (e.key == (i18n::language() == Lang::EN ? 'a' : 'e')) { on_command(M_SELALL, 0); return true; }
        }
        if (e.key == KEY_LEFT && (e.mods & MOD_ALT)) { go_back(); return true; }
        if (e.key == KEY_RIGHT && (e.mods & MOD_ALT)) { go_fwd(); return true; }
        return false;
    }
};
}

namespace apps {
void explorer(const char* path) {
    auto* w = new Explorer(path ? path : "C:\\");
    wm::add(w);
}
}

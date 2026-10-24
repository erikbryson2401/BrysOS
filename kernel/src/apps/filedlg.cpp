// Diálogos Abrir y Guardar como
#include "apps/common.h"

namespace {
enum { ID_LIST = 100, ID_NAME, ID_UP, ID_PATH, ID_NEWDIR };

class FileDialog : public Dialog {
public:
    bool save;
    String filter;
    String dir;
    String result;
    ListView* list;
    TextBox* name;
    TextBox* path_box;

    FileDialog(bool sv, const char* title, const char* flt, const char* start_dir, const char* start_name)
        : Dialog(title, 440, 300), save(sv), filter(flt ? flt : "") {
        add(new Label({8, 12, 70, 16}, save ? T("Guardar en:") : T("Buscar en:")));
        path_box = add(new TextBox({80, 8, 260, 21}, "", ID_PATH));
        Button* up = add(new Button({346, 7, 24, 23}, "", ID_UP));
        up->icon = &icon_up_folder.small;
        Button* nd = add(new Button({374, 7, 24, 23}, "", ID_NEWDIR));
        nd->icon = &icon_folder.small;
        list = add(new ListView({8, 36, 424, 180}, ID_LIST));
        list->mode = ListView::LIST;
        add(new Label({8, 228, 110, 16}, T("&Nombre de archivo:")));
        name = add(new TextBox({120, 224, 220, 21}, start_name ? start_name : "", ID_NAME));
        add(new Label({8, 256, 110, 16}, T("&Tipo de archivo:")));
        // el filtro puede tener varias extensiones: "WAV;MP3"
        String pats;
        for (size_t i = 0; i < filter.size(); i++) {
            if (i == 0 || filter[i - 1] == ';') pats += "*.";
            pats.append(to_lower(filter[i]));
        }
        String t = filter.empty() ? String(T("Todos los archivos (*.*)"))
                                  : String::format(T("%s (%s)"), type_name((String(T("x.")) + first_ext()).c_str(), false).c_str(), pats.c_str());
        Label* tl = add(new Label({120, 252, 220, 21}, t.c_str()));
        tl->sunken = true;
        tl->bg = col::WHITE;
        default_btn = add(new Button({350, 223, 82, 23}, save ? T("&Guardar") : T("&Abrir"), ID_OK));
        default_btn->is_default = true;
        add(new Button({350, 251, 82, 23}, T("Cancelar"), ID_CANCEL));
        focus = name;
        navigate(start_dir && vfs::is_dir(start_dir) ? start_dir : vfs::docs_dir().c_str());
    }

    String first_ext() const {
        String e;
        for (size_t i = 0; i < filter.size() && filter[i] != ';'; i++) e.append(filter[i]);
        return e;
    }
    bool matches(const String& ext) const {
        for (size_t i = 0; i <= filter.size();) {
            size_t j = i;
            while (j < filter.size() && filter[j] != ';') j++;
            bool same = j - i == ext.size();
            for (size_t k = 0; same && k < ext.size(); k++) same = to_upper(filter[i + k]) == to_upper(ext[k]);
            if (same) return true;
            i = j + 1;
        }
        return false;
    }

    void navigate(const char* d) {
        String nd = vfs::normalize(d);
        if (!vfs::is_dir(nd.c_str())) {
            msgbox(this, title, String::format(T("No se encuentra la carpeta \"%s\"."), nd.c_str()).c_str(), MB_OK, &icon_warning);
            return;
        }
        dir = nd;
        path_box->set_text(dir.c_str());
        Vec<VfsEntry> ents;
        vfs::list(dir.c_str(), ents);
        ents.sort([](const VfsEntry& a, const VfsEntry& b) {
            if (a.dir != b.dir) return a.dir ? -1 : 1;
            return stricmp(a.name.c_str(), b.name.c_str());
        });
        list->clear();
        for (auto& e : ents) {
            if (!e.dir && !filter.empty() && !matches(vfs::extension(e.name.c_str()))) continue;
            if (e.attr & 0x02) continue; // ocultos
            list->add(e.name.c_str(), icon_for(e.name.c_str(), e.dir), e.dir ? 1 : 0);
        }
        invalidate();
    }

    bool on_ok() override {
        String n = name->text;
        if (n.empty()) return false;
        String full = vfs::normalize(n.c_str(), dir.c_str());
        if (vfs::is_dir(full.c_str())) {
            navigate(full.c_str());
            name->set_text("");
            return false;
        }
        if (save) {
            if (!filter.empty() && vfs::extension(full.c_str()).empty()) {
                full += ".";
                String fe = first_ext();
                for (size_t i = 0; i < fe.size(); i++) full.append(to_lower(fe[i]));
            }
            if (vfs::exists(full.c_str())) {
                String q = String::format(T("%s ya existe.\n¿Desea reemplazarlo?"), vfs::basename(full.c_str()).c_str());
                if (msgbox(this, title, q.c_str(), MB_YESNO, &icon_warning) != ID_YES) return false;
            }
        } else if (!vfs::exists(full.c_str())) {
            msgbox(this, title, String::format(T("%s\nNo se encuentra el archivo.\nCompruebe el nombre e inténtelo de nuevo."), full.c_str()).c_str(), MB_OK, &icon_warning);
            return false;
        }
        result = full;
        return true;
    }

    void on_command(int id, int code) override {
        switch (id) {
        case ID_LIST: {
            int i = list->selected();
            if (code == LV_SELCHANGE && i >= 0 && !list->items[i].data) name->set_text(list->items[i].text.c_str());
            if (code == LV_ACTIVATE && i >= 0) {
                if (list->items[i].data) navigate(vfs::join(dir.c_str(), list->items[i].text.c_str()).c_str());
                else { name->set_text(list->items[i].text.c_str()); Dialog::on_command(ID_OK, 0); }
            }
            if (code == LV_BACK) navigate(vfs::parent(dir.c_str()).c_str());
            return;
        }
        case ID_UP: navigate(vfs::parent(dir.c_str()).c_str()); return;
        case ID_PATH: if (code == 1) navigate(path_box->text.c_str()); return;
        case ID_NAME: if (code == 1) Dialog::on_command(ID_OK, 0); return;
        case ID_NEWDIR: {
            String n = vfs::unique_name(dir.c_str(), T("Nueva carpeta"), "");
            if (!vfs::mkdir(n.c_str())) msgbox(this, title, vfs::error(), MB_OK, &icon_error);
            navigate(dir.c_str());
            return;
        }
        }
        Dialog::on_command(id, code);
    }
};
}

bool file_dialog(Window* owner, bool save, const char* title, const char* filter, String& path) {
    String start_dir, start_name;
    if (!path.empty()) {
        if (vfs::is_dir(path.c_str())) start_dir = path;
        else { start_dir = vfs::parent(path.c_str()); start_name = vfs::basename(path.c_str()); }
    }
    auto* d = new FileDialog(save, title, filter, start_dir.empty() ? nullptr : start_dir.c_str(),
                             start_name.empty() ? nullptr : start_name.c_str());
    if (owner) d->owner = owner;
    d->center(owner && owner->visible ? owner : nullptr);
    if (wm::run_modal(d) != ID_OK) return false;
    path = d->result;
    return true;
}

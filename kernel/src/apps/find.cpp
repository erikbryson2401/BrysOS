// Buscar: archivos o carpetas (por nombre con comodines y por contenido)
#include "apps/common.h"

namespace {
enum { ID_NAME = 1, ID_TEXT, ID_DIR, ID_SUB, ID_FIND, ID_STOP, ID_NEW, ID_LIST, ID_BROWSE,
       M_OPEN = 100, M_OPENDIR, M_PROPS };

class FindWin : public Window {
public:
    TextBox* name;
    TextBox* text;
    TextBox* dir;
    CheckBox* sub;
    Button* bfind;
    Button* bstop;
    ListView* list;
    Label* status;
    Menu ctx;
    bool running = false;
    bool stop = false;
    int found = 0;
    int scanned = 0;

    FindWin(const char* start) : Window(T("Buscar: Todos los archivos"), 600, 380, WF_NORMAL) {
        icon = &icon_find;
        min_w = 460;
        min_h = 260;
        add(new Label({12, 14, 90, 16}, T("&Nombre:")));
        name = add(new TextBox({104, 10, 300, 21}, "", ID_NAME));
        add(new Label({12, 42, 90, 16}, T("&Con el texto:")));
        text = add(new TextBox({104, 38, 300, 21}, "", ID_TEXT));
        add(new Label({12, 70, 90, 16}, T("&Buscar en:")));
        dir = add(new TextBox({104, 66, 300, 21}, start ? start : "C:\\", ID_DIR));
        sub = add(new CheckBox({104, 92, 200, 20}, T("&Incluir subcarpetas"), ID_SUB, true));
        bfind = add(new Button({0, 0, 100, 23}, T("B&uscar ahora"), ID_FIND));
        bfind->is_default = true;
        bstop = add(new Button({0, 0, 100, 23}, T("&Detener"), ID_STOP));
        bstop->enabled = false;
        add(new Button({0, 0, 100, 23}, T("Nue&va búsqueda"), ID_NEW));
        list = add(new ListView({0, 0, 10, 10}, ID_LIST));
        list->mode = ListView::DETAILS;
        list->columns.push({T("Nombre"), 160});
        list->columns.push({T("En la carpeta"), 200});
        list->columns.push({T("Tamaño"), 70, true});
        list->columns.push({T("Tipo"), 130});
        list->columns.push({T("Modificado"), 120});
        status = add(new Label({0, 0, 10, 10}, T("Escriba el nombre o parte del nombre (admite * y ?).")));
        status->sunken = true;
        ctx.add(T("&Abrir"), M_OPEN).add(T("Abrir la &carpeta contenedora"), M_OPENDIR).separator().add(T("&Propiedades"), M_PROPS);
        focus = name;
    }

    void on_resize() override {
        int w = client_w(), h = client_h();
        name->r.w = text->r.w = dir->r.w = w - 104 - 130;
        int bx = w - 116;
        widgets[7]->r = {bx, 10, 104, 23};   // Buscar ahora
        widgets[8]->r = {bx, 38, 104, 23};   // Detener
        widgets[9]->r = {bx, 66, 104, 23};   // Nueva búsqueda
        list->r = {0, 120, w, h - 120 - 20};
        status->r = {0, h - 20, w, 20};
    }

    void on_paint(Surface& s) override {
        s.clear(col::FACE);
        s.etched({4, 2, client_w() - 130, 114});
    }

    bool name_match(const char* pattern, const char* n) {
        if (!pattern[0]) return true;
        bool wild = false;
        for (const char* p = pattern; *p; p++) if (*p == '*' || *p == '?') wild = true;
        if (wild) return wildcard_match(pattern, n);
        return str_icontains(n, pattern);
    }

    bool content_match(const char* path, const char* needle) {
        uint8_t* d;
        uint32_t n;
        if (!vfs::read_file(path, d, n)) return false;
        for (uint32_t i = 0; i < n; i++) if (!d[i]) d[i] = ' ';
        bool r = str_icontains((const char*)d, needle);
        mm::kfree(d);
        return r;
    }

    void search(const String& folder, int depth) {
        if (stop || closing || depth > 24) return;
        Vec<VfsEntry> ents;
        if (!vfs::list(folder.c_str(), ents)) return;
        for (auto& e : ents) {
            if (stop || closing) return;
            String p = vfs::join(folder.c_str(), e.name.c_str());
            scanned++;
            bool ok = name_match(name->text.c_str(), e.name.c_str());
            if (ok && !text->text.empty()) ok = !e.dir && content_match(p.c_str(), text->text.c_str());
            if (ok) {
                ListItem& it = list->add(e.name.c_str(), icon_for(e.name.c_str(), e.dir));
                it.path = p;
                it.sub.push(folder);
                it.sub.push(e.dir ? String() : vfs::format_size(e.size));
                it.sub.push(type_name(e.name.c_str(), e.dir));
                it.sub.push(format_date(e.mtime));
                found++;
            }
            if ((scanned & 31) == 0) {
                status->set_text(String::format(T("Buscando en %s...  %d encontrado(s)"), folder.c_str(), found).c_str());
                wm::pump();
            }
            if (e.dir && sub->checked) search(p, depth + 1);
        }
    }

    void start() {
        if (running) return;
        String d = vfs::normalize(dir->text.c_str());
        if (!vfs::is_dir(d.c_str())) {
            msgbox(this, T("Buscar"), T("La carpeta indicada en \"Buscar en\" no existe."), MB_OK, &icon_warning);
            return;
        }
        if (name->text.empty() && text->text.empty()) {
            msgbox(this, T("Buscar"), T("Escriba un nombre de archivo o un texto para buscar."), MB_OK, &icon_info);
            return;
        }
        list->clear();
        running = true;
        stop = false;
        found = scanned = 0;
        bfind->enabled = false;
        bstop->enabled = true;
        invalidate();
        wm::set_busy(true);
        search(d, 0);
        wm::set_busy(false);
        if (closing) return;
        running = false;
        bfind->enabled = true;
        bstop->enabled = false;
        status->set_text(String::format(T("%d archivo(s) encontrado(s)%s"), found, stop ? T(" (búsqueda detenida)") : "").c_str());
        invalidate();
    }

    void on_command(int id, int code) override {
        switch (id) {
        case ID_FIND: start(); return;
        case ID_NAME: case ID_TEXT: case ID_DIR: if (code == 1) start(); return;
        case ID_STOP: stop = true; return;
        case ID_NEW:
            if (running) return;
            name->set_text("");
            text->set_text("");
            list->clear();
            status->set_text("");
            name->focus();
            return;
        case ID_LIST:
            if (code == LV_ACTIVATE) on_command(M_OPEN, 0);
            if (code == LV_CONTEXT && list->selected() >= 0) show_menu(&ctx, this, list->ctx_x, list->ctx_y);
            return;
        case M_OPEN: {
            int i = list->selected();
            if (i >= 0) apps::open_file(list->items[i].path.c_str());
            return;
        }
        case M_OPENDIR: {
            int i = list->selected();
            if (i >= 0) apps::explorer(vfs::parent(list->items[i].path.c_str()).c_str());
            return;
        }
        case M_PROPS: {
            int i = list->selected();
            if (i >= 0) apps::properties(list->items[i].path.c_str());
            return;
        }
        }
    }

    bool on_key(const InputEvent& e) override {
        if (e.pressed && e.key == KEY_ESC && running) { stop = true; return true; }
        return false;
    }

    bool on_close() override {
        stop = true;
        return true;
    }
};
}

namespace apps {
void find(const char* start) {
    wm::add(new FindWin(start));
}
}

// Bloc de notas
#include "apps/common.h"
#include "drivers/rtc.h"

namespace {
enum {
    ID_EDIT = 1,
    M_NEW = 100, M_OPEN, M_SAVE, M_SAVEAS, M_EXIT, M_CUT, M_COPY, M_PASTE, M_DELETE, M_SELALL, M_DATETIME,
    M_FIND, M_FINDNEXT, M_GOTO, M_ABOUT, M_STATUS,
};

class FindDialog;

class Notepad : public Window {
public:
    TextArea* edit;
    Label* status = nullptr;
    String path;
    String find_text;
    bool find_case = false;
    bool show_status = true;

    Notepad(const char* file) : Window(T("Sin título - Bloc de notas"), 560, 380, WF_NORMAL) {
        icon = &icon_notepad;
        auto* mb = new MenuBar();
        mb->add(T("&Archivo"))->add(T("&Nuevo"), M_NEW, T("Ctrl+U")).add(T("&Abrir..."), M_OPEN, T("Ctrl+A"))
            .add(T("&Guardar"), M_SAVE, T("Ctrl+G")).add(T("Guardar &como..."), M_SAVEAS).separator().add(T("&Salir"), M_EXIT);
        mb->add(T("&Edición"))->add(T("Cor&tar"), M_CUT, T("Ctrl+X")).add(T("&Copiar"), M_COPY, T("Ctrl+C")).add(T("&Pegar"), M_PASTE, T("Ctrl+V"))
            .add(T("&Eliminar"), M_DELETE, T("Supr")).separator().add(T("Seleccionar &todo"), M_SELALL, T("Ctrl+E"))
            .add(T("&Hora y fecha"), M_DATETIME, "F5");
        mb->add(T("&Buscar"))->add(T("&Buscar..."), M_FIND, T("Ctrl+B")).add(T("Buscar &siguiente"), M_FINDNEXT, "F3")
            .add(T("&Ir a la línea..."), M_GOTO, T("Ctrl+T"));
        mb->add(T("&Ver"))->add(T("&Barra de estado"), M_STATUS);
        mb->add(T("Ay&uda"))->add(T("&Acerca del Bloc de notas"), M_ABOUT);
        attach_menubar(this, mb);
        edit = add(new TextArea({0, 0, 10, 10}, ID_EDIT));
        status = add(new Label({0, 0, 10, 10}, ""));
        status->sunken = true;
        focus = edit;
        if (file) load(file);
        update_status();
    }

    void on_resize() override {
        int h = client_h() - (show_status ? 20 : 0);
        edit->r = {0, 0, client_w(), h};
        status->visible = show_status;
        status->r = {0, client_h() - 20, client_w(), 20};
    }

    void update_title() {
        String n = path.empty() ? String(T("Sin título")) : vfs::basename(path.c_str());
        set_title((n + T(" - Bloc de notas")).c_str());
    }

    void update_status() {
        if (!status) return;
        status->set_text(String::format(T("   Línea %d, columna %d"), edit->cy + 1, edit->cx + 1).c_str());
    }

    bool load(const char* p) {
        uint8_t* data;
        uint32_t size;
        if (!vfs::read_file(p, data, size)) {
            msgbox(this, T("Bloc de notas"), String::format(T("No se puede abrir %s.\n%s"), p, vfs::error()).c_str(), MB_OK, &icon_error);
            return false;
        }
        edit->set_text((const char*)data, size);
        mm::kfree(data);
        path = vfs::normalize(p);
        update_title();
        return true;
    }

    bool save_to(const char* p) {
        String text = edit->get_text();
        wm::set_busy(true);
        bool ok = vfs::write_file(p, text.c_str(), (uint32_t)text.size());
        wm::set_busy(false);
        if (!ok) {
            msgbox(this, T("Bloc de notas"), String::format(T("No se puede guardar %s.\n%s"), p, vfs::error()).c_str(), MB_OK, &icon_error);
            return false;
        }
        path = vfs::normalize(p);
        edit->modified = false;
        update_title();
        return true;
    }

    bool save(bool ask) {
        if (ask || path.empty()) {
            String p = path.empty() ? vfs::join(vfs::docs_dir().c_str(), T("Sin título.txt")) : path;
            if (!file_dialog(this, true, T("Guardar como"), "TXT", p)) return false;
            return save_to(p.c_str());
        }
        return save_to(path.c_str());
    }

    // Pregunta si guardar los cambios. false = cancelar la acción
    bool confirm_discard() {
        if (!edit->modified) return true;
        String n = path.empty() ? String(T("Sin título")) : path;
        String q = String::format(T("El texto del archivo %s ha cambiado.\n\n¿Desea guardar los cambios?"), n.c_str());
        int r = msgbox(this, T("Bloc de notas"), q.c_str(), MB_YESNOCANCEL, &icon_warning);
        if (r == ID_CANCEL) return false;
        if (r == ID_YES) return save(false);
        return true;
    }

    bool on_close() override { return confirm_discard(); }

    void find_next() {
        if (find_text.empty()) { on_command(M_FIND, 0); return; }
        if (!edit->find(find_text.c_str(), find_case, true))
            msgbox(this, T("Bloc de notas"), String::format(T("No se encuentra \"%s\""), find_text.c_str()).c_str(), MB_OK, &icon_info);
    }

    void on_menu_init(Menu* m) override {
        m->enable(M_CUT, edit->has_selection());
        m->enable(M_COPY, edit->has_selection());
        m->enable(M_DELETE, edit->has_selection());
        m->enable(M_PASTE, !wm::clipboard().empty());
        m->check(M_STATUS, show_status);
    }

    void on_command(int id, int code) override {
        switch (id) {
        case ID_EDIT: update_status(); return;
        case M_NEW:
            if (!confirm_discard()) return;
            edit->set_text("", 0);
            path.clear();
            update_title();
            return;
        case M_OPEN: {
            if (!confirm_discard()) return;
            String p = path.empty() ? vfs::docs_dir() : vfs::parent(path.c_str());
            if (file_dialog(this, false, T("Abrir"), "TXT", p)) load(p.c_str());
            return;
        }
        case M_SAVE: save(false); return;
        case M_SAVEAS: save(true); return;
        case M_EXIT: close(); return;
        case M_CUT: edit->cut(); return;
        case M_COPY: edit->copy(); return;
        case M_PASTE: edit->paste(); return;
        case M_DELETE: edit->delete_selection(); edit->invalidate(); edit->modified = true; return;
        case M_SELALL: edit->select_all(); return;
        case M_DATETIME: {
            DateTime t = rtc::now();
            edit->insert(String::format(T("%d:%02d %02d/%02d/%04d"), t.hour, t.minute, t.day, t.month, t.year).c_str());
            return;
        }
        case M_FIND: {
            String s = find_text;
            if (edit->has_selection()) s = edit->selected_text();
            if (input_box(this, T("Buscar"), T("Buscar:"), s) && !s.empty()) {
                find_text = s;
                find_next();
            }
            return;
        }
        case M_FINDNEXT: find_next(); return;
        case M_GOTO: {
            String s = String::format(T("%d"), edit->cy + 1);
            if (input_box(this, T("Ir a la línea"), T("Número de línea:"), s)) {
                int n = 0;
                for (const char* p = s.c_str(); *p >= '0' && *p <= '9'; p++) n = n * 10 + (*p - '0');
                if (n < 1 || n > (int)edit->lines.size())
                    msgbox(this, T("Bloc de notas - Ir a la línea"), T("El número de línea supera el total de líneas."), MB_OK, &icon_info);
                else edit->goto_line(n - 1);
            }
            return;
        }
        case M_STATUS: show_status = !show_status; on_resize(); invalidate(); return;
        case M_ABOUT:
            msgbox(this, T("Acerca del Bloc de notas"), T("Bloc de notas de BrysOS\nVersión 1.0\n\nEditor de texto sin formato."), MB_OK, &icon_notepad);
            return;
        }
    }

    bool on_key(const InputEvent& e) override {
        if (!e.pressed) return false;
        if (e.key == KEY_F3) { find_next(); return true; }
        if (e.key == KEY_F5) { on_command(M_DATETIME, 0); return true; }
        if (e.mods & MOD_CTRL) {
            // atajos de Windows en español o en inglés, según el idioma
            bool en = i18n::language() == Lang::EN;
            uint16_t k = e.key;
            if (k == (en ? 's' : 'g')) { on_command(M_SAVE, 0); return true; }
            if (k == (en ? 'o' : 'a')) { on_command(M_OPEN, 0); return true; }
            if (k == (en ? 'n' : 'u')) { on_command(M_NEW, 0); return true; }
            if (k == (en ? 'f' : 'b')) { on_command(M_FIND, 0); return true; }
            if (k == (en ? 'g' : 't')) { on_command(M_GOTO, 0); return true; }
            if (k == (en ? 'a' : 'e')) { on_command(M_SELALL, 0); return true; }
        }
        return false;
    }
};
}

namespace apps {
void notepad(const char* path) {
    wm::add(new Notepad(path));
}
}

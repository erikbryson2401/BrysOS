// Programas .run: arranque y "Cerrar programa" (Ctrl+Alt+Supr), como en Windows 98
#include "apps/common.h"
#include "proc/process.h"

namespace {
enum { ID_LIST = 10, ID_END = 11 };

class CloseProgramDlg : public Dialog {
public:
    ListView* list;
    explicit CloseProgramDlg() : Dialog(T("Cerrar programa"), 400, 270) {
        flags |= WF_TOPMOST;
        add(new Label({12, 10, 376, 16}, T("Programas en ejecución:")));
        list = add(new ListView({12, 28, 376, 150}, ID_LIST));
        list->mode = ListView::LIST;
        add(new Label({12, 184, 376, 44},
                      T("ADVERTENCIA: si pulsa \"Finalizar tarea\", el programa se cierra al\n"
                        "momento y se pierde la información que no haya guardado.")));
        default_btn = add(new Button({156, 236, 110, 23}, T("&Finalizar tarea"), ID_END));
        default_btn->is_default = true;
        add(new Button({274, 236, 110, 23}, T("Cancelar"), ID_CANCEL));
        refresh();
        wm::set_timer(this, 1, 1000);
        focus = list;
    }

    void refresh() {
        int sel = list->selected();
        int64_t sel_pid = sel >= 0 ? list->items[sel].data : -1;
        list->clear();
        int k = 0, pick = -1;
        for (Process* p : process::list()) {
            if (p->dying) continue;
            String t = p->name;
            if (!process::responding(p)) t += T(" (no responde)");
            list->add(t.c_str(), &icon_app, p->pid);
            if (p->pid == sel_pid) pick = k;
            k++;
        }
        if (list->items.empty()) list->add(T("(ningún programa .run en marcha)"), nullptr, -1);
        list->select(pick >= 0 ? pick : 0);
        invalidate();
    }

    void on_timer(int) override { refresh(); }

    void on_command(int id, int code) override {
        if (id == ID_END || (id == ID_LIST && code == LV_ACTIVATE)) {
            int i = list->selected();
            if (i < 0) return;
            int64_t pid = list->items[i].data;
            for (Process* p : process::list())
                if (p->pid == pid) process::kill(p);
            refresh();
            return;
        }
        Dialog::on_command(id, code);
    }
};
}

namespace apps {
bool run_program(const char* path, const char* args) {
    String err;
    Process* p = process::start(path, args, err);
    if (!p) {
        msgbox(nullptr, vfs::basename(path).c_str(),
               String::format(T("No se puede ejecutar el programa.\n\n%s"), err.c_str()).c_str(), MB_OK, &icon_error);
        return false;
    }
    return true;
}

void close_program_dialog() {
    for (Window* w : wm::windows())
        if (!strcmp(w->title, T("Cerrar programa"))) { wm::activate(w); return; }
    auto* d = new CloseProgramDlg();
    d->owner = nullptr;
    d->center();
    wm::add(d);
    wm::activate(d);
}
}

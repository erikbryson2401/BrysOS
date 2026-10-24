#include "gui/dialogs.h"

Dialog::Dialog(const char* title, int w, int h) : Window(title, w, h, WF_DIALOGBOX) {
    owner = wm::active();
}

void Dialog::end(int result) {
    modal_result = result;
    wm::destroy(this);
}

void Dialog::on_command(int id, int) {
    if (id == ID_OK && !on_ok()) return;
    if (id == ID_OK || id == ID_CANCEL || id == ID_YES || id == ID_NO) end(id);
}

bool Dialog::on_key(const InputEvent& e) {
    if (!e.pressed) return false;
    if (e.key == KEY_ESC) { on_command(ID_CANCEL, 0); return true; }
    if (e.key == KEY_ENTER && default_btn) { on_command(default_btn->id, 0); return true; }
    return false;
}

// ---------------------------------------------------------------------------
// Cuadro de mensaje
// ---------------------------------------------------------------------------
namespace {
class MsgBox : public Dialog {
public:
    Vec<String> lines;
    const IconPair* ic;
    int buttons;
    volatile int* result_out = nullptr;   // sin bucle modal: el resultado se escribe aquí
    void on_destroyed() override {
        if (result_out) *result_out = modal_result ? modal_result : (buttons == MB_OK ? ID_OK : ID_CANCEL);
    }
    MsgBox(const char* title, const char* text, int btns, const IconPair* icon)
        : Dialog(title, 100, 100), ic(icon), buttons(btns) {
        // Ajuste de línea a 380 píxeles
        const Font& f = font_ui;
        int maxw = 0;
        const char* p = text;
        while (*p) {
            const char* e = p;
            while (*e && *e != '\n') e++;
            // parte la línea por palabras
            const char* s = p;
            while (s < e) {
                const char* cut = s;
                const char* last_space = nullptr;
                while (cut < e && f.text_width(s, (int)(cut - s + 1)) <= 380) {
                    if (*cut == ' ') last_space = cut;
                    cut++;
                }
                if (cut < e && last_space) cut = last_space + 1;
                if (cut == s) cut++;
                lines.push(String(s, (size_t)(cut - s)));
                maxw = max(maxw, f.text_width(s, (int)(cut - s)));
                s = cut;
            }
            if (s == p) lines.push(String());
            p = *e ? e + 1 : e;
        }
        int nb = btns == MB_OK ? 1 : (btns == MB_YESNOCANCEL ? 3 : 2);
        int text_h = (int)lines.size() * f.height;
        int cw = max(maxw + 70, nb * 81 + 30);
        cw = max(cw, 220);
        int ch = max(text_h, 32) + 24 + 23 + 16;
        resize_client(cw, ch);
        const char* labels[3];
        int ids[3];
        switch (btns) {
        case MB_OKCANCEL: labels[0] = T("Aceptar"); ids[0] = ID_OK; labels[1] = T("Cancelar"); ids[1] = ID_CANCEL; break;
        case MB_YESNO: labels[0] = T("&Sí"); ids[0] = ID_YES; labels[1] = T("&No"); ids[1] = ID_NO; break;
        case MB_YESNOCANCEL:
            labels[0] = T("&Sí"); ids[0] = ID_YES; labels[1] = T("&No"); ids[1] = ID_NO;
            labels[2] = T("Cancelar"); ids[2] = ID_CANCEL; break;
        default: labels[0] = T("Aceptar"); ids[0] = ID_OK; break;
        }
        int bx = (cw - (nb * 75 + (nb - 1) * 6)) / 2;
        for (int i = 0; i < nb; i++) {
            Button* b = add(new Button({bx + i * 81, ch - 23 - 10, 75, 23}, labels[i], ids[i]));
            if (i == 0) { b->is_default = true; default_btn = b; focus = b; }
        }
    }
    void on_paint(Surface& s) override {
        s.clear(col::FACE);
        if (ic) draw_icon(s, ic->big, 12, 12);
        int ty = 12 + (lines.size() == 1 ? 9 : 0);
        for (size_t i = 0; i < lines.size(); i++)
            s.text(font_ui, 58, ty + (int)i * font_ui.height, lines[i].c_str(), col::BLACK);
    }
    bool on_key(const InputEvent& e) override {
        if (e.pressed && e.key == KEY_ESC) {
            end(buttons == MB_YESNO ? ID_NO : (buttons == MB_OK ? ID_OK : ID_CANCEL));
            return true;
        }
        if (e.pressed && (buttons == MB_YESNO || buttons == MB_YESNOCANCEL)) {
            if (e.key == 's' || e.key == 'S') { end(ID_YES); return true; }
            if (e.key == 'n' || e.key == 'N') { end(ID_NO); return true; }
        }
        return Dialog::on_key(e);
    }
};

class InputDlg : public Dialog {
public:
    TextBox* tb;
    InputDlg(const char* title, const char* prompt, const char* value) : Dialog(title, 340, 110) {
        add(new Label({12, 12, 316, 30}, prompt));
        tb = add(new TextBox({12, 44, 316, 21}, value));
        tb->select_all();
        default_btn = add(new Button({172, 76, 75, 23}, T("Aceptar"), ID_OK));
        default_btn->is_default = true;
        add(new Button({253, 76, 75, 23}, T("Cancelar"), ID_CANCEL));
        focus = tb;
    }
};
}

int msgbox(Window* owner, const char* title, const char* text, int buttons, const IconPair* icon) {
    MsgBox* m = new MsgBox(title, text, buttons, icon);
    if (owner) m->owner = owner;
    m->center(owner && owner->visible && !(owner->flags & WF_DESKTOP) ? owner : nullptr);
    return wm::run_modal(m);
}

Window* msgbox_async(Window* owner, const char* title, const char* text, int buttons, const IconPair* icon,
                     volatile int* result) {
    MsgBox* m = new MsgBox(title, text, buttons, icon);
    m->owner = owner;
    m->result_out = result;
    m->center(owner && owner->visible && !(owner->flags & WF_DESKTOP) ? owner : nullptr);
    wm::add(m);
    wm::activate(m);
    return m;
}

bool input_box(Window* owner, const char* title, const char* prompt, String& value) {
    InputDlg* d = new InputDlg(title, prompt, value.c_str());
    if (owner) d->owner = owner;
    d->center(owner && owner->visible && !(owner->flags & WF_DESKTOP) ? owner : nullptr);
    // la ventana sólo se libera al volver al bucle principal: tb sigue siendo válido
    int r = wm::run_modal(d);
    if (r == ID_OK) value = d->tb->text;
    return r == ID_OK;
}

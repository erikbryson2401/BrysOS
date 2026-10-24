// Créditos y licencias: lo que BrysOS incluye de otros proyectos, con sus avisos de copyright.
// Los textos están en licencias/*.txt y van dentro del núcleo (tools/genlicenses.py).
// Se muestra en el primer arranque del sistema instalado (hasta que se marque "No volver a
// mostrar") y desde la Ayuda.
#include "apps/common.h"
#include "apps/settings.h"

struct EmbeddedText {
    const char* name;
    const uint8_t* data;
    uint64_t size;
};
extern "C" const EmbeddedText licenses_embedded[];
extern "C" const uint64_t licenses_embedded_count;

namespace {
// Entrada de licencias/NN-nombre.txt: "Nombre:", "es:", "en:", "---" y el texto original
struct Credit {
    String name, desc_es, desc_en;
    String text;
};

Vec<Credit> load_credits() {
    Vec<Credit> out;
    for (uint64_t i = 0; i < licenses_embedded_count; i++) {
        String all((const char*)licenses_embedded[i].data, (size_t)licenses_embedded[i].size);
        Credit c;
        size_t pos = 0;
        while (pos < all.size()) {
            int nl = all.find('\n', pos);
            size_t end = nl < 0 ? all.size() : (size_t)nl;
            String line = all.substr(pos, end - pos);
            pos = end + 1;
            const char* l = line.c_str();
            if (!strcmp(l, "---")) break;
            if (!strncmp(l, "Nombre: ", 8)) c.name = l + 8;
            else if (!strncmp(l, "es: ", 4)) c.desc_es = l + 4;
            else if (!strncmp(l, "en: ", 4)) c.desc_en = l + 4;
        }
        if (pos < all.size()) c.text = all.substr(pos, all.size() - pos);
        if (c.name.empty()) c.name = licenses_embedded[i].name;
        out.push(c);
    }
    return out;
}

// Parte un párrafo en líneas de como mucho 'width' caracteres (el texto va en letra fija)
String wrap(const char* s, int width) {
    String out;
    while (*s) {
        int n = (int)strlen(s);
        if (n <= width) { out += s; break; }
        int cut = width;
        while (cut > 0 && s[cut] != ' ') cut--;
        if (cut == 0) cut = width;
        out += String(s, (size_t)cut);
        out += "\n";
        s += cut;
        while (*s == ' ') s++;
    }
    return out;
}

constexpr int ID_LIST = 1, ID_TEXT = 2, ID_HIDE = 3;

class CreditsDlg : public Dialog {
public:
    Vec<Credit> credits;
    ListView* list;
    TextArea* text;
    CheckBox* hide;
    bool first_boot;

    explicit CreditsDlg(bool first) : Dialog(T("Créditos y licencias"), 800, 460), first_boot(first) {
        icon = &icon_help;
        if (first) flags |= WF_NOCLOSE;   // en el primer arranque sólo se cierra con Aceptar
        else flags &= ~WF_NO_TASKBAR;
        credits = load_credits();
        add(new Label({12, 10, 776, 32},
                      T("BrysOS incluye componentes de otros proyectos. Estos son sus autores y las licencias con las\n"
                        "que se distribuyen. Puede volver a ver esta ventana desde Inicio > Ayuda.")));
        list = add(new ListView({12, 48, 180, 360}, ID_LIST));
        list->mode = ListView::LIST;
        for (auto& c : credits) list->add(c.name.c_str(), &icon_file_text);
        text = add(new TextArea({198, 48, 590, 360}, ID_TEXT));
        text->read_only = true;
        hide = add(new CheckBox({12, 424, 420, 20}, T("&No volver a mostrar esta ventana al iniciar BrysOS"), ID_HIDE,
                                settings::credits_hidden));
        default_btn = add(new Button({713, 422, 75, 23}, T("Aceptar"), ID_OK));
        default_btn->is_default = true;
        focus = list;
        if (!credits.empty()) show(0);
    }

    void show(int i) {
        list->select(i);
        const Credit& c = credits[(size_t)i];
        const String& desc = i18n::language() == Lang::EN && !c.desc_en.empty() ? c.desc_en : c.desc_es;
        String t = c.name + "\n\n" + wrap(desc.c_str(), 76) + "\n\n" + c.text;
        text->set_text(t.c_str(), t.size());
    }

    void on_command(int id, int code) override {
        if (id == ID_LIST && code == LV_SELCHANGE && list->selected() >= 0) { show(list->selected()); return; }
        if (id == ID_CANCEL && first_boot) return;   // Esc no la cierra en el primer arranque
        Dialog::on_command(id, code);
    }

    bool on_ok() override {
        if (hide->checked != settings::credits_hidden) {
            settings::credits_hidden = hide->checked;
            settings::save();
        }
        return true;
    }
};
}

namespace apps {
void credits(bool first_boot) {
    if (!first_boot) {
        for (Window* w : wm::windows())
            if (!strcmp(w->title, T("Créditos y licencias"))) { wm::activate(w); return; }
    }
    auto* d = new CreditsDlg(first_boot);
    d->owner = nullptr;
    d->center();
    if (first_boot) wm::run_modal(d);
    else wm::add(d);
}
}

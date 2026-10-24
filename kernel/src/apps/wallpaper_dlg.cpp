// Fondo de escritorio: imagen, modo (centrado, mosaico, expandido, ajustado) y vista previa
#include "apps/common.h"
#include "apps/settings.h"
#include "gui/wallpaper.h"
#include "gui/desktop.h"

namespace {
enum { ID_LIST = 10, ID_BROWSE, ID_MODE0 = 20, ID_APPLY = 30 };
constexpr Rect MON{116, 12, 188, 142};         // monitor de la vista previa
constexpr Rect SCR{MON.x + 14, MON.y + 12, MON.w - 28, MON.h - 40};

class WallpaperDlg : public Dialog {
public:
    ListView* list;
    Vec<CheckBox*> modes;
    Vec<String> paths;          // paths[i] del elemento i de la lista ("" = ninguno)
    Surface img;                // imagen elegida (reducida) para la vista previa
    bool img_ok = false;
    String loaded_path;         // la que hay en 'img'
    Surface prev;
    int mode;

    explicit WallpaperDlg(const char* preset) : Dialog(T("Fondo de escritorio"), 420, 400) {
        flags &= ~WF_NO_TASKBAR;
        icon = &icon_settings;
        mode = settings::wallpaper_mode;
        add(new Label({12, 166, 200, 16}, T("&Fondo (imágenes de Mis imágenes):")));
        list = add(new ListView({12, 184, 256, 168}, ID_LIST));
        list->mode = ListView::LIST;
        add(new Button({278, 184, 130, 23}, T("&Examinar..."), ID_BROWSE));
        add(new Label({278, 220, 130, 16}, T("&Mostrar:")));
        const char* names[4] = {"Centrado", "Mosaico", "Expandido", "Ajustado"};
        for (int i = 0; i < 4; i++) {
            auto* cb = add(new CheckBox({286, 238 + i * 22, 120, 20}, T(names[i]), ID_MODE0 + i, mode == i));
            cb->radio = true;
            modes.push(cb);
        }
        default_btn = add(new Button({172, 364, 75, 23}, T("Aceptar"), ID_OK));
        default_btn->is_default = true;
        add(new Button({253, 364, 75, 23}, T("Cancelar"), ID_CANCEL));
        add(new Button({334, 364, 75, 23}, T("A&plicar"), ID_APPLY));
        fill_list(preset);
        focus = list;
    }

    void add_item(const String& p, const char* label) {
        list->add(label, p.empty() ? nullptr : &icon_file_image, (int64_t)paths.size());
        paths.push(p);
    }

    void fill_list(const char* preset) {
        add_item(String(), T("(Ninguno)"));
        int sel = settings::wallpaper ? -1 : 0;
        // imágenes de Mis imágenes y de Mis documentos
        String dirs[2] = {vfs::pictures_dir(), vfs::docs_dir()};
        for (auto& d : dirs) {
            Vec<VfsEntry> ents;
            if (!vfs::list(d.c_str(), ents)) continue;
            ents.sort([](const VfsEntry& a, const VfsEntry& b) { return stricmp(a.name.c_str(), b.name.c_str()); });
            for (auto& e : ents)
                if (!e.dir && apps::image_file(vfs::extension(e.name.c_str()).c_str()))
                    add_item(vfs::join(d.c_str(), e.name.c_str()), e.name.c_str());
        }
        if (settings::wallpaper && wallpaper::current()) {
            // el fondo actual (la copia guardada) como primer elemento tras "(Ninguno)"
            list->items.insert(1, ListItem());
            ListItem& it = list->items[1];
            it.text = T("(Fondo actual)");
            it.icon = &icon_file_image;
            it.data = -1;
            sel = 1;
        }
        if (preset && preset[0]) {
            String p = vfs::normalize(preset);
            int found = -1;
            for (size_t i = 0; i < paths.size(); i++)
                if (!stricmp(paths[i].c_str(), p.c_str())) found = (int)i;
            if (found < 0) {
                add_item(p, vfs::basename(p.c_str()).c_str());
                found = (int)paths.size() - 1;
            }
            for (size_t k = 0; k < list->items.size(); k++)
                if (list->items[k].data == found) sel = (int)k;
        }
        list->select(sel < 0 ? 0 : sel);
        selection_changed();
    }

    // ruta del elemento elegido: "" ninguno, "*" el fondo actual
    String chosen() {
        int i = list->selected();
        if (i < 0) return String();
        int64_t d = list->items[i].data;
        if (d < 0) return String("*");
        return paths[(size_t)d];
    }

    void selection_changed() {
        String p = chosen();
        if (p.empty() || p == "*") {
            img_ok = p == "*" && wallpaper::current();
            if (img_ok) {
                const Surface* c = wallpaper::current();
                img.resize(c->w, c->h);
                memcpy(img.px, c->px, (size_t)c->w * c->h * 4);
            }
            loaded_path = p;
        } else if (p != loaded_path.c_str()) {
            String err;
            wm::set_busy(true);
            img_ok = wallpaper::load_image(p.c_str(), img, err);
            wm::set_busy(false);
            loaded_path = p;
            if (!img_ok) msgbox(this, T("Fondo de escritorio"), err.c_str(), MB_OK, &icon_error);
        }
        update_preview();
    }

    void update_preview() {
        prev.resize(SCR.w, SCR.h);
        if (img_ok) wallpaper::compose_preview(prev, img, mode, settings::desktop_color);
        else prev.clear(settings::desktop_color);
        invalidate();
    }

    void on_paint(Surface& s) override {
        s.clear(col::FACE);
        // monitor
        s.fill(MON, col::FACE);
        s.bevel_raised(MON);
        s.fill({MON.x + 60, MON.bottom(), MON.w - 120, 8}, col::FACE);
        s.bevel_raised({MON.x + 40, MON.bottom() + 6, MON.w - 80, 6});
        s.bevel_sunken({SCR.x - 2, SCR.y - 2, SCR.w + 4, SCR.h + 4});
        s.blit(prev, SCR.x, SCR.y);
        s.fill({MON.right() - 22, MON.bottom() - 14, 6, 4}, 0x00C000);
    }

    bool apply() {
        String p = chosen();
        if (p.empty()) {
            wallpaper::clear();
        } else if (p == "*") {
            wallpaper::set_mode(mode);
        } else {
            String err;
            wm::set_busy(true);
            bool ok = wallpaper::set(p.c_str(), mode, err);
            wm::set_busy(false);
            if (!ok) {
                msgbox(this, T("Fondo de escritorio"), err.c_str(), MB_OK, &icon_error);
                return false;
            }
        }
        settings::wallpaper_mode = mode;
        if (!settings::save())
            msgbox(this, T("Fondo de escritorio"), String::format(T("No se pudo guardar la configuración: %s"), vfs::error()).c_str(),
                   MB_OK, &icon_warning);
        return true;
    }

    bool on_ok() override { return apply(); }

    void on_command(int id, int code) override {
        if (id == ID_LIST && code == LV_SELCHANGE) { selection_changed(); return; }
        if (id == ID_LIST && code == LV_ACTIVATE) { on_command(ID_OK, 0); return; }
        if (id >= ID_MODE0 && id < ID_MODE0 + 4) {
            mode = id - ID_MODE0;
            for (int i = 0; i < 4; i++) modes[i]->checked = i == mode;
            update_preview();
            return;
        }
        if (id == ID_BROWSE) {
            String p = vfs::pictures_dir();
            if (file_dialog(this, false, T("Examinar"), nullptr, p)) {
                p = vfs::normalize(p.c_str());
                int found = -1;
                for (size_t i = 0; i < paths.size(); i++)
                    if (!stricmp(paths[i].c_str(), p.c_str())) found = (int)i;
                if (found < 0) {
                    add_item(p, vfs::basename(p.c_str()).c_str());
                    found = (int)paths.size() - 1;
                }
                for (size_t k = 0; k < list->items.size(); k++)
                    if (list->items[k].data == found) list->select((int)k);
                selection_changed();
            }
            return;
        }
        if (id == ID_APPLY) { apply(); return; }
        Dialog::on_command(id, code);
    }
};
}

namespace apps {
void wallpaper_dialog(const char* path) {
    for (Window* w : wm::windows())
        if (!strcmp(w->title, T("Fondo de escritorio"))) { wm::activate(w); return; }
    auto* d = new WallpaperDlg(path);
    d->owner = nullptr;
    d->center();
    wm::add(d);
}
}

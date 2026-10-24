// Actualizador de BrysOS: abre un paquete .bsi (normalmente desde un pendrive), muestra lo
// que contiene y lo aplica sobre C:\BrysOS.
#include "apps/common.h"
#include "sys/bsi.h"
#include "drivers/acpi.h"
#include "drivers/block.h"
#include "fs/fat32.h"
#include "kernel.h"

namespace {
enum { ID_APPLY = 100, ID_REBOOT = 101 };

class Updater : public Dialog {
public:
    bsi::Package& pkg;
    String installed;
    bool can_apply = true;
    bool busy = false;
    bool done = false;
    Label* verdict;
    Label* status;
    ProgressBar* bar;
    Button* apply_btn;
    Button* cancel_btn;

    Updater(bsi::Package& p, const char* file) : Dialog(T("Actualizador de BrysOS"), 440, 350), pkg(p) {
        flags &= ~WF_NO_TASKBAR;
        icon = &icon_package;
        installed = bsi::installed_version();
        add(new Label({60, 14, 370, 16}, T("Se va a actualizar BrysOS con este paquete:")));
        String info = String::format(T("Paquete:\t%s\nArchivo:\t%s\nVersión del paquete:\t%s\nVersión instalada:\t%s\nContenido:\t%d archivo(s), %s; %d para borrar"),
                                     pkg.name.c_str(), file, pkg.version.c_str(), installed.c_str(), (int)pkg.files.size(),
                                     vfs::format_size(pkg.bytes()).c_str(), (int)pkg.deletes.size());
        // columnas: etiqueta y valor
        int y = 40;
        const char* s = info.c_str();
        while (*s) {
            const char* e = s;
            while (*e && *e != '\n') e++;
            String line(s, (size_t)(e - s));
            int tab = line.find('\t');
            add(new Label({60, y, 130, 16}, line.substr(0, (size_t)tab).c_str()));
            add(new Label({190, y, 240, 16}, line.substr((size_t)tab + 1).c_str()));
            y += 18;
            s = *e ? e + 1 : e;
        }
        auto* desc = add(new TextArea({14, y + 6, 412, 82}));
        desc->read_only = true;
        desc->font = &font_ui;
        String d = pkg.description.empty() ? String(T("(El paquete no incluye ninguna descripción.)")) : pkg.description;
        desc->set_text(d.c_str(), d.size());
        y += 96;

        int cmp = bsi::compare_versions(pkg.version.c_str(), installed.c_str());
        String v;
        if (vfs::system_on_ram()) {
            v = T("BrysOS no está instalado: se está ejecutando desde el medio de instalación.\nInstálelo primero y aplique la actualización después.");
            can_apply = false;
        } else if (!pkg.min_version.empty() && bsi::compare_versions(installed.c_str(), pkg.min_version.c_str()) < 0) {
            v = String::format(T("Este paquete necesita BrysOS %s o posterior. Instale antes las\nactualizaciones intermedias."), pkg.min_version.c_str());
            can_apply = false;
        } else if (cmp > 0) {
            v = T("El paquete es más reciente que la versión instalada.");
        } else if (cmp == 0) {
            v = T("Esta versión ya está instalada. Puede volver a aplicarla para reparar\nlos archivos del sistema.");
        } else {
            v = T("Atención: el paquete es ANTERIOR a la versión instalada. Si continúa,\nBrysOS volverá a esa versión.");
        }
        verdict = add(new Label({14, y, 412, 32}, v.c_str()));
        y += 36;
        status = add(new Label({14, y, 412, 16}, ""));
        bar = add(new ProgressBar({14, y + 18, 412, 18}));
        bar->visible = false;
        apply_btn = add(new Button({266, 316, 75, 23}, T("&Instalar"), ID_APPLY));
        cancel_btn = add(new Button({351, 316, 75, 23}, T("Cancelar"), ID_CANCEL));
        apply_btn->enabled = can_apply;
        if (can_apply) {
            apply_btn->is_default = true;
            default_btn = apply_btn;
        } else {
            cancel_btn->is_default = true;
            default_btn = cancel_btn;
        }
    }

    void on_paint(Surface& s) override {
        Dialog::on_paint(s);
        draw_icon(s, icon_package.big, 14, 12);
    }

    bool on_close() override { return !busy; }

    bool on_key(const InputEvent& e) override {
        if (busy) return true;
        return Dialog::on_key(e);
    }

    static void progress(void* ctx, int pct, const char* what) {
        auto* u = (Updater*)ctx;
        u->bar->set(pct);
        u->status->set_text(what);
        wm::pump();
    }

    bool needs_reboot() const { return pkg.reboot || pkg.replaces_kernel(); }

    void run_update() {
        if (bsi::compare_versions(pkg.version.c_str(), installed.c_str()) < 0 &&
            msgbox(this, T("Actualizador de BrysOS"),
                   String::format(T("¿Seguro que desea volver de BrysOS %s a la versión %s?"), installed.c_str(), pkg.version.c_str()).c_str(),
                   MB_YESNO, &icon_warning) != ID_YES)
            return;
        busy = true;
        apply_btn->enabled = false;
        cancel_btn->enabled = false;
        bar->visible = true;
        wm::set_busy(true);
        String err;
        bool ok = bsi::apply(pkg, progress, this, err);
        wm::set_busy(false);
        busy = false;
        cancel_btn->enabled = true;
        if (!ok) {
            status->set_text(T("La actualización no se ha completado."));
            msgbox(this, T("Actualizador de BrysOS"), err.c_str(), MB_OK, &icon_error);
            return;
        }
        done = true;
        verdict->set_text(String::format(T("BrysOS se ha actualizado a la versión %s."), pkg.version.c_str()).c_str());
        if (needs_reboot()) {
            status->set_text(T("Debe reiniciar el equipo para empezar a usar la nueva versión."));
            apply_btn->text = T("&Reiniciar");
            apply_btn->id = ID_REBOOT;
            apply_btn->enabled = true;
            cancel_btn->text = T("Más tarde");
        } else {
            status->set_text(T("Ya puede seguir usando el equipo."));
            apply_btn->visible = false;
            cancel_btn->text = T("Aceptar");
        }
        invalidate();
    }

    void on_command(int id, int code) override {
        if (busy) return;
        if (id == ID_APPLY) { run_update(); return; }
        if (id == ID_REBOOT) {
            for (Volume* v : vfs::volumes()) if (!v->read_only) v->fs->sync();
            for (BlockDevice* d : block::disks()) d->flush();
            acpi::reboot();
        }
        Dialog::on_command(id, code);
    }
};
}

namespace apps {
void updater(const char* path) {
    auto* pkg = new bsi::Package();
    String err;
    wm::set_busy(true);
    bool ok = bsi::load(path, *pkg, err);
    wm::set_busy(false);
    if (!ok) {
        msgbox(nullptr, T("Actualizador de BrysOS"), String::format(T("No se puede abrir \"%s\".\n\n%s"), path, err.c_str()).c_str(),
               MB_OK, &icon_error);
        delete pkg;
        return;
    }
    // ventana modal: mientras se aplica no se puede tocar nada más
    auto* u = new Updater(*pkg, vfs::basename(path).c_str());
    u->center();
    wm::run_modal(u);
    delete pkg;
}
}

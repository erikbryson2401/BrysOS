#include "gui/power_ui.h"
#include "gui/wm.h"
#include "gui/widgets.h"
#include "gui/desktop.h"
#include "acpi/power.h"
#include "apps/settings.h"
#include "arch/idt.h"
#include "lib/i18n.h"
#include "lib/notice.h"
#include "lib/string.h"
#include "lib/vec.h"

namespace {
class PowerPopup;
PowerPopup* popup = nullptr;
uint64_t last_poll = 0;
uint64_t brightness_changed = 0;   // cambio de brillo sin guardar
int warned = 100;                  // último aviso de batería baja (10 % y 5 %)
// estado dibujado en la barra de tareas (para redibujarla sólo si cambia)
int shown_percent = -2;
bool shown_ac = false, shown_charging = false;

String time_text(int minutes) {
    if (minutes >= 60) return String::format(T("%d h %02d min"), minutes / 60, minutes % 60);
    return String::format(T("%d min"), minutes);
}

String status_text() {
    const power::Battery& b = power::battery();
    if (!b.present) return String(T("No hay ninguna batería puesta."));
    if (b.charging) {
        if (b.minutes > 0) return String::format(T("Cargando (faltan unos %s)"), time_text(b.minutes).c_str());
        return String(T("Cargando"));
    }
    if (b.discharging) {
        if (b.minutes > 0) return String::format(T("Quedan unos %s"), time_text(b.minutes).c_str());
        return String(T("Usando la batería"));
    }
    if (b.percent >= 95) return String(T("Completamente cargada"));
    return String(T("Sin cargar ni descargar"));
}

class PowerPopup : public Window {
public:
    Label* source;
    Label* status;
    TrackBar* bright = nullptr;
    int levels[64];
    int nlevels = 0;

    PowerPopup() : Window(T("Medidor de energía"), 240, power::has_brightness() ? 168 : 110,
                          WF_POPUP | WF_TOPMOST | WF_NO_TASKBAR | WF_NOBORDER) {
        add(new Label({10, 8, 220, 16}, T("Medidor de energía")));
        source = add(new Label({10, 28, 220, 16}, ""));
        status = add(new Label({10, 80, 220, 16}, ""));
        if (power::has_brightness()) {
            add(new Label({10, 104, 220, 16}, T("Brillo de la pantalla:")));
            nlevels = power::brightness_levels(levels, 64);
            bright = add(new TrackBar({6, 120, 228, 40}, 10));
            bright->minimum = 0;
            bright->maximum = nlevels - 1;
            bright->page = 1;
            bright->tick_every = 1;
            bright->set(level_index(power::brightness()));
            focus = bright;
        }
        refresh();
    }

    int level_index(int pct) {
        int best = nlevels - 1;
        for (int i = 0; i < nlevels; i++)
            if (pct >= 0 && abs_val(levels[i] - pct) < abs_val(levels[best] - pct)) best = i;
        return best;
    }

    void refresh() {
        source->set_text(power::on_ac() ? T("Fuente de energía: corriente alterna") : T("Fuente de energía: batería"));
        status->set_text(status_text().c_str());
        invalidate();
    }

    Rect bar_rect() const { return {10, 50, 220, 22}; }

    void on_paint(Surface& s) override {
        s.clear(col::FACE);
        s.bevel_raised({0, 0, client_w(), client_h()});
        const power::Battery& b = power::battery();
        Rect r = bar_rect();
        s.fill(r, col::WHITE);
        s.bevel_thin_sunken(r);
        if (b.present && b.percent >= 0) {
            int w = (r.w - 4) * b.percent / 100;
            uint32_t c = b.percent <= 10 ? 0xC00000 : (b.percent <= 25 ? 0xE0A000 : 0x000080);
            s.fill({r.x + 2, r.y + 2, w, r.h - 4}, c);
            char t[16];
            ksnprintf(t, sizeof t, "%d %%", b.percent);
            int tw = font_ui.text_width(t);
            s.text(font_ui, r.x + (r.w - tw) / 2, r.y + 4, t, b.percent > 55 ? col::WHITE : col::BLACK);
        }
    }

    void on_command(int id, int) override {
        if (id == 10 && bright) {
            int pct = levels[bright->value];
            if (power::set_brightness(pct)) {
                settings::brightness = pct;
                brightness_changed = pit::ticks();
            }
        }
    }

    void on_destroyed() override { popup = nullptr; }
};

void save_if_needed() {
    if (brightness_changed && pit::ticks() - brightness_changed > 2000) {
        brightness_changed = 0;
        settings::save();
    }
}
}

void power_ui::init() {
    if (settings::brightness >= 0 && power::has_brightness()) power::set_brightness(settings::brightness);
}

bool power_ui::tray_visible() { return power::has_battery(); }

void power_ui::draw_tray_icon(Surface& s, int x, int y) {
    // pila de 16x16 en vertical (como en Windows 98) con la carga; un enchufe si hay corriente
    const power::Battery& b = power::battery();
    uint32_t k = col::BLACK;
    Rect body{x + 4, y + 3, 8, 13};
    s.fill({x + 6, y + 1, 4, 2}, k);
    s.fill(body, col::WHITE);
    s.hline(body.x, body.y, body.w, k);
    s.hline(body.x, body.bottom() - 1, body.w, k);
    s.vline(body.x, body.y, body.h, k);
    s.vline(body.right() - 1, body.y, body.h, k);
    int pct = b.percent < 0 ? 0 : b.percent;
    int h = (body.h - 2) * pct / 100;
    uint32_t c = pct <= 10 ? 0xC00000 : (pct <= 25 ? 0xE0C000 : 0x00A000);
    if (h > 0) s.fill({body.x + 1, body.bottom() - 1 - h, body.w - 2, h}, c);
    if (power::on_ac()) {
        // rayo amarillo con borde negro
        int bx = x + 1, by = y + 6;
        s.line(bx + 4, by, bx + 1, by + 5, k, 1);
        s.line(bx + 1, by + 5, bx + 4, by + 5, k, 1);
        s.line(bx + 4, by + 5, bx + 2, by + 9, k, 1);
    }
}

void power_ui::show_popup() {
    if (popup) { wm::close_popups(); return; }
    if (wm::popup_open()) wm::close_popups();
    power::poll();
    popup = new PowerPopup();
    int x = wm::screen_w() - popup->frame.w - 60;
    int y = wm::screen_h() - TASKBAR_H - popup->frame.h - 2;
    popup->move_to(x, y);
    wm::open_popup(popup);
}

void power_ui::tick() {
    save_if_needed();
    uint64_t now = pit::ticks();
    if (now - last_poll < 5000) return;
    last_poll = now;
    power::poll();
    const power::Battery& b = power::battery();
    if (b.percent != shown_percent || power::on_ac() != shown_ac || b.charging != shown_charging) {
        shown_percent = b.percent;
        shown_ac = power::on_ac();
        shown_charging = b.charging;
        if (taskbar()) taskbar()->invalidate();
        if (popup) popup->refresh();
    }
    // avisos de batería baja, una vez por nivel mientras se descarga
    if (b.present && b.discharging && b.percent >= 0) {
        if (b.percent <= 5 && warned > 5) {
            warned = 5;
            notice::post(T("Batería muy baja"),
                         String::format(T("Queda un %d %% de batería.\n\nGuarde su trabajo y conecte el equipo a la corriente, "
                                          "o apáguelo: está a punto de quedarse sin energía."), b.percent).c_str(), true);
        } else if (b.percent <= 10 && warned > 10) {
            warned = 10;
            notice::post(T("Batería baja"),
                         String::format(T("Queda un %d %% de batería. Conecte el equipo a la corriente."), b.percent).c_str(), false);
        }
    }
    if (power::on_ac() || b.charging) warned = 100;
}

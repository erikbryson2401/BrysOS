#include "gui/volume.h"
#include "gui/wm.h"
#include "gui/widgets.h"
#include "gui/desktop.h"
#include "gui/icons.h"
#include "drivers/audio.h"
#include "drivers/input.h"
#include "apps/settings.h"
#include "arch/idt.h"
#include "lib/i18n.h"
#include "lib/string.h"

namespace {
constexpr int STEP = 5;
uint64_t changed_at = 0;   // última modificación sin guardar (0 = nada pendiente)

class VolumePopup;
VolumePopup* popup = nullptr;

void apply_and_mark() {
    settings::volume = audio::volume();
    settings::muted = audio::muted();
    changed_at = pit::ticks();
    if (taskbar()) taskbar()->invalidate();
}

class VolumePopup : public Window {
public:
    TrackBar* bar;
    CheckBox* mute;
    bool auto_close;

    explicit VolumePopup(bool from_key) : Window(T("Volumen"), 78, 150, WF_POPUP | WF_TOPMOST | WF_NO_TASKBAR | WF_NOBORDER),
                                          auto_close(from_key) {
        add(new Label({0, 6, 78, 16}, T("Volumen"), 1));
        bar = add(new TrackBar({18, 26, 42, 92}, 10, true));
        bar->tick_every = 20;
        bar->set(audio::volume());
        mute = add(new CheckBox({8, 124, 68, 20}, T("&Silencio"), 11, audio::muted()));
        focus = bar;
        if (auto_close) wm::set_timer(this, 1, 1500);
    }

    void on_paint(Surface& s) override {
        s.clear(col::FACE);
        s.bevel_raised({0, 0, client_w(), client_h()});
    }

    void sync() {
        bar->set(audio::volume());
        mute->checked = audio::muted();
        mute->invalidate();
        if (auto_close) wm::set_timer(this, 1, 1500);   // otra pulsación: sigue abierto
    }

    void on_command(int id, int) override {
        if (id == 10) audio::set_volume(bar->value);
        else if (id == 11) audio::set_mute(mute->checked);
        auto_close = false;   // el usuario lo está usando: ya no se cierra solo
        wm::kill_timer(this, 1);
        apply_and_mark();
    }

    void on_timer(int) override {
        wm::kill_timer(this, 1);
        if (auto_close) wm::close_popups(this);
    }

    void on_destroyed() override { popup = nullptr; }
};

// Teclas multimedia en cualquier ventana
bool hotkey(const InputEvent& e) {
    if (!e.pressed) return false;
    if (e.key == KEY_VOLUME_UP) { volume_ui::change(STEP); return true; }
    if (e.key == KEY_VOLUME_DOWN) { volume_ui::change(-STEP); return true; }
    if (e.key == KEY_MUTE) { volume_ui::toggle_mute(); return true; }
    return false;
}
}

void volume_ui::init() { wm::set_hotkey_handler(hotkey); }

void volume_ui::change(int delta) {
    audio::set_volume(audio::volume() + delta);
    if (delta > 0) audio::set_mute(false);
    apply_and_mark();
    show_popup(true);
}

void volume_ui::toggle_mute() {
    audio::set_mute(!audio::muted());
    apply_and_mark();
    show_popup(true);
}

void volume_ui::show_popup(bool from_key) {
    if (popup) { popup->sync(); return; }
    if (wm::popup_open()) wm::close_popups();
    popup = new VolumePopup(from_key);
    int x = wm::screen_w() - popup->frame.w - 40;
    int y = wm::screen_h() - TASKBAR_H - popup->frame.h - 2;
    popup->move_to(x, y);
    wm::open_popup(popup);
}

void volume_ui::draw_tray_icon(Surface& s, int x, int y) {
    // altavoz de 16x16 al estilo de Windows 98: caja, cono y ondas
    // (o una prohibición roja si está en silencio)
    uint32_t k = col::BLACK, body = 0xE0E0E0, cone = 0xA0A0A0;
    s.fill({x + 1, y + 5, 4, 6}, body);
    s.hline(x + 1, y + 4, 4, k);
    s.hline(x + 1, y + 11, 4, k);
    s.vline(x, y + 4, 8, k);
    for (int i = 0; i < 5; i++) {
        int top = y + 4 - i, len = 8 + 2 * i;
        s.vline(x + 5 + i, top + 1, len - 2, i == 0 ? body : cone);
        s.put(x + 5 + i, top, k);
        s.put(x + 5 + i, top + len - 1, k);
    }
    s.vline(x + 10, y, 16, k);
    if (audio::muted() || audio::volume() == 0) {
        s.line(x + 11, y + 4, x + 15, y + 11, 0xCC0000, 2);
        s.line(x + 15, y + 4, x + 11, y + 11, 0xCC0000, 2);
        return;
    }
    // ondas: arcos aproximados, más cuanto más alto está el volumen
    int waves = audio::volume() > 66 ? 2 : (audio::volume() > 33 ? 1 : 0);
    s.vline(x + 12, y + 6, 4, k);
    if (waves >= 1) {
        s.put(x + 13, y + 4, k);
        s.vline(x + 14, y + 5, 6, k);
        s.put(x + 13, y + 11, k);
    }
    if (waves >= 2) {
        s.put(x + 14, y + 2, k);
        s.vline(x + 15, y + 3, 10, k);
        s.put(x + 14, y + 13, k);
    }
}

void volume_ui::tick() {
    // se guarda cuando lleva un par de segundos sin cambiar (no en cada pulsación)
    if (!changed_at || pit::ticks() - changed_at < 2000) return;
    changed_at = 0;
    settings::save();
    settings::update_boot_line();
}

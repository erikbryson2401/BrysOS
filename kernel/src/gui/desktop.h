#pragma once
#include "gui/wm.h"
#include "gui/menu.h"

struct DesktopIcon {
    String name;
    const IconPair* icon = nullptr;
    String target;          // ruta o "::accion"
    int x = 0, y = 0;
    bool selected = false;
};

class Desktop : public Window {
public:
    Vec<DesktopIcon> icons;
    uint32_t bg_color = col::DESKTOP;
    Menu ctx, icon_ctx;
    int ctx_icon = -1;
    int drag_icon = -1, drag_dx = 0, drag_dy = 0, drag_sx = 0, drag_sy = 0;
    bool dragging = false;

    Desktop();
    void add_icon(const char* name, const IconPair* icon, const char* target);
    void rebuild();      // vuelve a crear iconos y menús (cambio de idioma)
    void arrange();
    void refresh();
    void set_color(uint32_t c);
    int icon_at(int x, int y);
    void open_icon(int i);
    void on_paint(Surface& s) override;
    void on_mouse_down(const MouseEv& e) override;
    void on_mouse_move(const MouseEv& e) override;
    void on_mouse_up(const MouseEv& e) override;
    bool on_key(const InputEvent& e) override;
    void on_command(int id, int code) override;
};

class Taskbar : public Window {
public:
    Menu* start;
    Menu task_ctx;
    Window* ctx_window = nullptr;
    Vec<Window*> tasks;
    bool start_open = false;
    uint64_t closed_time = 0;
    int last_minute = -1;
    Vec<String> run_paths;   // programas .run del menú Inicio

    Taskbar();
    Menu* build_start_menu();
    void rebuild();
    void sync_tasks();
    Rect start_rect() const;
    Rect tray_rect() const;
    Rect task_rect(int i) const;
    void open_start();
    void toggle_start();
    void start_closed();
    void on_paint(Surface& s) override;
    void on_mouse_down(const MouseEv& e) override;
    void on_timer(int id) override;
    void on_command(int id, int code) override;
};

void desktop_init();
Desktop* desktop();
Taskbar* taskbar();

#pragma once
#include "gfx/gfx.h"
#include "gui/icons.h"
#include "drivers/input.h"
#include "lib/vec.h"

enum Cursor : uint8_t {
    CURSOR_ARROW, CURSOR_IBEAM, CURSOR_SIZE_H, CURSOR_SIZE_V, CURSOR_SIZE_D1, CURSOR_SIZE_D2,
    CURSOR_WAIT, CURSOR_CROSS, CURSOR_HAND, CURSOR_COUNT
};

struct MouseEv {
    int x = 0, y = 0;      // coordenadas locales (del widget o del área cliente)
    int sx = 0, sy = 0;    // coordenadas de pantalla
    int button = 0;        // botón que cambió (MB_*)
    int clicks = 1;        // 2 = doble clic
    uint8_t buttons = 0;   // botones pulsados
    uint8_t mods = 0;
    int wheel = 0;
};

class Window;

// Control dentro de una ventana. Las coordenadas de 'r' son del área cliente.
class Widget {
public:
    Rect r;
    Window* win = nullptr;
    int id = 0;
    bool visible = true;
    bool enabled = true;
    bool focusable = false;

    virtual ~Widget() {}
    virtual void paint(Surface& s) {}  // origen en la esquina del widget
    virtual void mouse_down(const MouseEv&) {}
    virtual void mouse_up(const MouseEv&) {}
    virtual void mouse_move(const MouseEv&) {}
    virtual void mouse_wheel(const MouseEv&) {}
    virtual bool key(const InputEvent&) { return false; }
    virtual void focus_changed(bool) {}
    virtual Cursor cursor(int x, int y) { return CURSOR_ARROW; }
    virtual void tick() {}  // llamado periódicamente (parpadeo del cursor, etc.)
    virtual void retranslate() {}  // cambio de idioma: vuelve a traducir sus textos

    bool has_focus() const;
    void invalidate();
    void notify(int code = 0);
    void focus();
};

enum WinFlags : uint32_t {
    WF_TITLE = 1, WF_RESIZABLE = 2, WF_MINIMIZE = 4, WF_MAXIMIZE = 8, WF_POPUP = 16,
    WF_TOPMOST = 32, WF_DESKTOP = 64, WF_NO_TASKBAR = 128, WF_DIALOG = 256, WF_NOBORDER = 512,
    WF_NOCLOSE = 1024,
};
constexpr uint32_t WF_NORMAL = WF_TITLE | WF_RESIZABLE | WF_MINIMIZE | WF_MAXIMIZE;
constexpr uint32_t WF_FIXED = WF_TITLE | WF_MINIMIZE;
constexpr uint32_t WF_DIALOGBOX = WF_TITLE | WF_DIALOG | WF_NO_TASKBAR;

constexpr int BORDER = 4;
constexpr int TITLE_H = 18;
constexpr int MENUBAR_H = 19;
constexpr int TASKBAR_H = 28;

class MenuBar;
class Menu;

class Window {
public:
    Rect frame;                 // en coordenadas de pantalla
    Surface surf;               // contenido completo (marco + cliente)
    uint32_t flags;
    char title[128];
    const IconPair* icon = &icon_app;
    bool visible = false;
    bool minimized = false;
    bool maximized = false;
    bool dirty = true;
    bool closing = false;
    Rect restore_frame;
    Window* owner = nullptr;    // ventana propietaria (diálogos)
    int modal_result = 0;
    Vec<Widget*> widgets;
    Widget* focus = nullptr;
    MenuBar* menubar = nullptr;
    int min_w = 140, min_h = 80;

    Window(const char* title, int client_w, int client_h, uint32_t flags = WF_NORMAL);
    virtual ~Window();

    // Geometría
    int border() const;
    Rect client_rect() const;                   // coordenadas locales de la ventana
    int client_w() const { return client_rect().w; }
    int client_h() const { return client_rect().h; }
    void set_title(const char* t);
    void center(Window* relative_to = nullptr);
    void move_to(int x, int y);
    void resize_client(int cw, int ch);

    // Controles
    template <typename T> T* add(T* w) { w->win = this; widgets.push(w); if (!focus && w->focusable) focus = w; return w; }
    void set_focus(Widget* w);
    void invalidate() { dirty = true; }
    void close();   // pide el cierre (llama a on_close)

    // Eventos (coordenadas del área cliente)
    virtual void on_paint(Surface& s) { s.clear(col::FACE); }
    virtual void on_mouse_down(const MouseEv&) {}
    virtual void on_mouse_up(const MouseEv&) {}
    virtual void on_mouse_move(const MouseEv&) {}
    virtual void on_mouse_wheel(const MouseEv&) {}
    virtual bool on_key(const InputEvent&) { return false; }
    virtual void on_command(int id, int code) {}
    virtual void on_resize() {}
    virtual bool on_close() { return true; }
    virtual void on_timer(int id) {}
    virtual void on_activate(bool active) {}
    virtual void on_destroyed() {}  // retirada de pantalla (antes de liberarse)
    virtual void on_menu_init(Menu*) {}  // antes de mostrar un menú desplegable
    // Cambio de idioma: título, controles y menús se traducen solos; las ventanas que
    // generan textos propios lo amplían (y llaman a Window::retranslate)
    virtual void retranslate();
    virtual Cursor client_cursor(int x, int y) { return CURSOR_ARROW; }

    // Uso interno del gestor
    void render();
    void paint_frame(bool active);
};

namespace wm {
void init();
[[noreturn]] void run();
// Bucle modal: procesa eventos hasta que 'w' se cierre. Devuelve w->modal_result.
int run_modal(Window* w);
// Procesa los eventos pendientes una vez (para operaciones largas)
void pump();

void add(Window* w);
void activate(Window* w);
void minimize(Window* w);
void restore(Window* w);
void toggle_maximize(Window* w);
void destroy(Window* w);          // cierra sin preguntar
Window* active();
Vec<Window*>& windows();
Rect work_area();                  // pantalla sin la barra de tareas
int screen_w();
int screen_h();
void dirty(const Rect& screen_rect);

// Temporizadores
void set_timer(Window* w, int id, uint32_t interval_ms);
void kill_timer(Window* w, int id);

// Ventanas emergentes (menús): se cierran al hacer clic fuera
void open_popup(Window* p);
void close_popups(Window* from = nullptr);  // cierra 'from' y las abiertas después (o todas)
bool popup_open();

void set_busy(bool busy);          // cursor de reloj de arena
void set_taskbar(Window* tb);
void set_start_handler(void (*fn)());
// Atajos globales (teclas multimedia): si devuelve true, la tecla no llega a las ventanas
void set_hotkey_handler(bool (*fn)(const InputEvent&));
// Vuelve a traducir todas las ventanas y menús abiertos (tras cambiar de idioma)
void retranslate_all();
void set_mouse_speed(int s);

// Portapapeles de texto
String& clipboard();
}

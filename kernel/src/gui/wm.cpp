// Gestor de ventanas: orden Z, foco, arrastre, redimensionado, composición
// por regiones sucias, cursor, temporizadores y bucles modales.
#include "gui/wm.h"
#include "lib/i18n.h"
#include "gui/menu.h"
#include "proc/sched.h"
#include "proc/process.h"
#include "apps/apps.h"
#include "arch/idt.h"
#include "arch/io.h"
#include "lib/log.h"
#include "drivers/usb.h"

// ---------------------------------------------------------------------------
// Cursores (# negro, o blanco, espacio transparente)
// ---------------------------------------------------------------------------
namespace {
struct CursorImg {
    int hx, hy;
    const char* rows[24];
};

const CursorImg cursors[CURSOR_COUNT] = {
    {0, 0, {"#           ", "##          ", "#o#         ", "#oo#        ", "#ooo#       ", "#oooo#      ",
            "#ooooo#     ", "#oooooo#    ", "#ooooooo#   ", "#oooooooo#  ", "#ooooo#####", "#oo#oo#     ",
            "#o# #oo#    ", "##  #oo#    ", "#    #oo#   ", "     #oo#   ", "      #oo#  ", "      #oo#  ",
            "       ##   ", nullptr}},
    {4, 8, {"### ###", "   #   ", "   #   ", "   #   ", "   #   ", "   #   ", "   #   ", "   #   ", "   #   ",
            "   #   ", "   #   ", "   #   ", "   #   ", "   #   ", "   #   ", "   #   ", "### ###", nullptr}},
    {10, 4, {"    #           #    ", "   ##           ##   ", "  #o#############o#  ", " #ooooooooooooooooo# ",
             "#ooooooooooooooooooo#", " #ooooooooooooooooo# ", "  #o#############o#  ", "   ##           ##   ",
             "    #           #    ", nullptr}},
    {4, 10, {"    #    ", "   #o#   ", "  #ooo#  ", " #ooooo# ", "#ooooooo#", "####o####", "   #o#   ", "   #o#   ",
             "   #o#   ", "   #o#   ", "   #o#   ", "   #o#   ", "   #o#   ", "   #o#   ", "   #o#   ", "####o####",
             "#ooooooo#", " #ooooo# ", "  #ooo#  ", "   #o#   ", "    #    ", nullptr}},
    {7, 7, {"#######        ", "#ooooo#        ", "#oooo#         ", "#ooo#          ", "#oo#o#         ",
            "#o#  #o#       ", "##    #o#      ", "       #o#     ", "        #o#    ", "         #o#  #",
            "          #o#o#", "           #oo#", "          #ooo#", "         #oooo#", "        #######", nullptr}},
    {7, 7, {"        #######", "        #ooooo#", "         #oooo#", "          #ooo#", "         #o#oo#",
            "       #o#  #o#", "      #o#    ##", "     #o#       ", "    #o#        ", "#  #o#         ",
            "#o#o#          ", "#oo#           ", "#ooo#          ", "#oooo#         ", "#######        ", nullptr}},
    {6, 10, {"#############", "#ooooooooooo#", " ########### ", " #ooooooooo# ", " #ooo#o#ooo# ", "  #ooo#ooo#  ",
             "   #ooooo#   ", "    #ooo#    ", "     #o#     ", "    #o#o#    ", "   #oo#oo#   ", "  #ooo#ooo#  ",
             " #ooo#o#ooo# ", " #oo#o#o#oo# ", " #ooooooooo# ", " ########### ", "#ooooooooooo#", "#############",
             nullptr}},
    {7, 7, {"       #       ", "       #       ", "       #       ", "       #       ", "       #       ",
            "               ", "               ", "#####     #####", "               ", "               ",
            "       #       ", "       #       ", "       #       ", "       #       ", "       #       ", nullptr}},
    {5, 0, {"     ##      ", "    #oo#     ", "    #oo#     ", "    #oo#     ", "    #oo###   ", "    #oo#oo## ",
            "### #oo#oo#o#", "#oo##oooooo#o#", "#ooo#ooooooooo#", " #oo#oooooooo#", " #oooooooooooo#",
            "  #ooooooooooo#", "  #oooooooooo# ", "   #ooooooooo# ", "   #oooooooo#  ", "    #ooooooo#  ",
            "    #########  ", nullptr}},
};

// ---------------------------------------------------------------------------
// Estado del gestor
// ---------------------------------------------------------------------------
Vec<Window*> wins;            // de abajo arriba
Window* active_win = nullptr;
Vec<Window*> popups;
Vec<Window*> modal_stack;
Vec<Window*> graveyard;
Window* taskbar = nullptr;
void (*start_handler)() = nullptr;
bool (*hotkey_handler)(const InputEvent&) = nullptr;

int mx = 0, my = 0;
uint8_t mbuttons = 0;
Cursor cur_cursor = CURSOR_ARROW;
bool busy = false;
int mouse_speed = 2; // 1 lento, 2 normal, 3 rápido

constexpr int MAX_DIRTY = 24;
Rect dirty_list[MAX_DIRTY];
int ndirty = 0;

struct Timer {
    Window* w;
    int id;
    uint32_t interval;
    uint64_t next;
};
Vec<Timer> timers;
uint64_t next_tick = 0;
String clip;

enum class Drag { NONE, MOVE, RESIZE, CAPBTN, WIDGET, CLIENT } drag = Drag::NONE;
Window* drag_win = nullptr;
Widget* drag_widget = nullptr;
int drag_ox = 0, drag_oy = 0;
int resize_edges = 0; // 1 izq, 2 der, 4 arriba, 8 abajo
Rect drag_start;
int drag_smx = 0, drag_smy = 0;
int capbtn = 0;      // 1 cerrar, 2 maximizar, 3 minimizar
bool capbtn_hover = false;

uint64_t last_click_time = 0;
int last_click_x = -100, last_click_y = -100, last_click_btn = 0;
int cascade = 0;

enum Hit { H_NONE, H_CLIENT, H_TITLE, H_CLOSE, H_MAX, H_MIN, H_BORDER, H_MENUBAR };

Rect cursor_rect() {
    const CursorImg& c = cursors[busy ? CURSOR_WAIT : cur_cursor];
    int w = 0, h = 0;
    for (; c.rows[h]; h++) w = max(w, (int)strlen(c.rows[h]));
    return {mx - c.hx, my - c.hy, w, h};
}

void draw_cursor(Surface& s) {
    const CursorImg& c = cursors[busy ? CURSOR_WAIT : cur_cursor];
    for (int y = 0; c.rows[y]; y++)
        for (int x = 0; c.rows[y][x]; x++) {
            char ch = c.rows[y][x];
            if (ch == '#') s.put(mx - c.hx + x, my - c.hy + y, col::BLACK);
            else if (ch == 'o') s.put(mx - c.hx + x, my - c.hy + y, col::WHITE);
        }
}

bool is_top_layer(Window* w) { return w->flags & (WF_TOPMOST | WF_POPUP); }

void raise(Window* w) {
    if (w->flags & WF_DESKTOP) return;
    wins.remove(w);
    if (is_top_layer(w)) {
        if (w->flags & WF_POPUP) { wins.push(w); return; }
        // las ventanas siempre visibles quedan bajo los menús emergentes
        size_t i = wins.size();
        while (i > 0 && (wins[i - 1]->flags & WF_POPUP)) i--;
        wins.insert(i, w);
        return;
    }
    size_t i = wins.size();
    while (i > 0 && is_top_layer(wins[i - 1])) i--;
    wins.insert(i, w);
}

Window* window_at(int x, int y) {
    for (size_t i = wins.size(); i > 0; i--) {
        Window* w = wins[i - 1];
        if (w->visible && !w->minimized && w->frame.contains(x, y)) return w;
    }
    return nullptr;
}

// Rectángulos de los botones del título (coordenadas locales)
Rect cap_rect(Window* w, int which) {
    int b = w->border();
    int y = b + 2;
    int x = w->frame.w - b - 2 - 16;
    if (which == 1) return {x, y, 16, 14};
    if (w->flags & WF_NOCLOSE) x += 18;
    x -= 18;
    if (which == 2) return (w->flags & WF_MAXIMIZE) ? Rect{x, y, 16, 14} : Rect{};
    if (w->flags & WF_MAXIMIZE) x -= 16;
    if (which == 3) return (w->flags & WF_MINIMIZE) ? Rect{x, y, 16, 14} : Rect{};
    return {};
}

int hit_test(Window* w, int lx, int ly, int& edges) {
    edges = 0;
    if (w->flags & (WF_POPUP | WF_NOBORDER)) return H_CLIENT;
    Rect cr = w->client_rect();
    if (cr.contains(lx, ly)) return H_CLIENT;
    int b = w->border();
    if (w->flags & WF_TITLE) {
        if (!(w->flags & WF_NOCLOSE) && cap_rect(w, 1).contains(lx, ly)) return H_CLOSE;
        if (cap_rect(w, 2).contains(lx, ly)) return H_MAX;
        if (cap_rect(w, 3).contains(lx, ly)) return H_MIN;
        if (Rect{b, b, w->frame.w - 2 * b, TITLE_H}.contains(lx, ly)) return H_TITLE;
    }
    if (w->menubar && Rect{b, cr.y - MENUBAR_H, cr.w, MENUBAR_H}.contains(lx, ly)) return H_MENUBAR;
    if ((w->flags & WF_RESIZABLE) && !w->maximized) {
        int corner = 16;
        if (lx < b) edges |= 1;
        if (lx >= w->frame.w - b) edges |= 2;
        if (ly < b) edges |= 4;
        if (ly >= w->frame.h - b) edges |= 8;
        if (edges & (1 | 2)) {
            if (ly < corner) edges |= 4;
            if (ly >= w->frame.h - corner) edges |= 8;
        }
        if (edges & (4 | 8)) {
            if (lx < corner) edges |= 1;
            if (lx >= w->frame.w - corner) edges |= 2;
        }
        if (edges) return H_BORDER;
    }
    return H_NONE;
}

Cursor edge_cursor(int e) {
    if ((e & 5) == 5 || (e & 10) == 10) return CURSOR_SIZE_D1;
    if ((e & 6) == 6 || (e & 9) == 9) return CURSOR_SIZE_D2;
    if (e & 3) return CURSOR_SIZE_H;
    if (e & 12) return CURSOR_SIZE_V;
    return CURSOR_ARROW;
}

Widget* widget_at(Window* w, int cx, int cy) {
    for (size_t i = w->widgets.size(); i > 0; i--) {
        Widget* wd = w->widgets[i - 1];
        if (wd->visible && wd->r.contains(cx, cy)) return wd;
    }
    return nullptr;
}

MouseEv make_ev(int lx, int ly, int button, int clicks) {
    MouseEv e;
    e.x = lx;
    e.y = ly;
    e.sx = mx;
    e.sy = my;
    e.button = button;
    e.clicks = clicks;
    e.buttons = mbuttons;
    e.mods = input::modifiers();
    return e;
}

void set_cursor(Cursor c) {
    if (c == cur_cursor) return;
    wm::dirty(cursor_rect());
    cur_cursor = c;
    wm::dirty(cursor_rect());
}

bool blocked_by_modal(Window* w) {
    if (modal_stack.empty()) return false;
    Window* m = modal_stack.back();
    if (w == m || (w->flags & WF_POPUP) || w == taskbar) return false;
    // ventanas propiedad del diálogo modal (p. ej. otro diálogo) sí se permiten
    for (Window* o = w->owner; o; o = o->owner)
        if (o == m) return false;
    return true;
}

void update_hover_cursor() {
    if (drag == Drag::MOVE) { set_cursor(CURSOR_ARROW); return; }
    if (drag == Drag::RESIZE) { set_cursor(edge_cursor(resize_edges)); return; }
    Window* w = drag_win && drag != Drag::NONE ? drag_win : window_at(mx, my);
    if (!w) { set_cursor(CURSOR_ARROW); return; }
    int lx = mx - w->frame.x, ly = my - w->frame.y, edges;
    int h = hit_test(w, lx, ly, edges);
    if (h == H_BORDER) { set_cursor(edge_cursor(edges)); return; }
    if (h == H_CLIENT) {
        Rect cr = w->client_rect();
        int cx = lx - cr.x, cy = ly - cr.y;
        Widget* wd = drag == Drag::WIDGET ? drag_widget : widget_at(w, cx, cy);
        if (wd) set_cursor(wd->cursor(cx - wd->r.x, cy - wd->r.y));
        else set_cursor(w->client_cursor(cx, cy));
        return;
    }
    set_cursor(CURSOR_ARROW);
}

void apply_resize() {
    Window* w = drag_win;
    int dx = mx - drag_smx, dy = my - drag_smy;
    Rect f = drag_start;
    if (resize_edges & 1) { int nw = max(w->min_w, f.w - dx); f.x = f.right() - nw; f.w = nw; }
    if (resize_edges & 2) f.w = max(w->min_w, f.w + dx);
    if (resize_edges & 4) { int nh = max(w->min_h, f.h - dy); f.y = f.bottom() - nh; f.h = nh; }
    if (resize_edges & 8) f.h = max(w->min_h, f.h + dy);
    if (f.x == w->frame.x && f.y == w->frame.y && f.w == w->frame.w && f.h == w->frame.h) return;
    wm::dirty(w->frame);
    w->frame = f;
    w->on_resize();
    w->invalidate();
}

void handle_move() {
    switch (drag) {
    case Drag::MOVE: {
        Window* w = drag_win;
        int nx = mx - drag_ox, ny = clamp(my - drag_oy, 0, wm::screen_h() - TASKBAR_H - 8);
        if (nx != w->frame.x || ny != w->frame.y) {
            wm::dirty(w->frame);
            w->frame.x = nx;
            w->frame.y = ny;
            wm::dirty(w->frame);
        }
        break;
    }
    case Drag::RESIZE: apply_resize(); break;
    case Drag::CAPBTN: {
        bool h = cap_rect(drag_win, capbtn).contains(mx - drag_win->frame.x, my - drag_win->frame.y);
        if (h != capbtn_hover) { capbtn_hover = h; drag_win->invalidate(); }
        break;
    }
    case Drag::WIDGET: {
        Rect cr = drag_win->client_rect();
        drag_widget->mouse_move(make_ev(mx - drag_win->frame.x - cr.x - drag_widget->r.x,
                                        my - drag_win->frame.y - cr.y - drag_widget->r.y, 0, 0));
        break;
    }
    case Drag::CLIENT: {
        Rect cr = drag_win->client_rect();
        drag_win->on_mouse_move(make_ev(mx - drag_win->frame.x - cr.x, my - drag_win->frame.y - cr.y, 0, 0));
        break;
    }
    case Drag::NONE: {
        Window* w = window_at(mx, my);
        if (w && !blocked_by_modal(w)) {
            int edges;
            int lx = mx - w->frame.x, ly = my - w->frame.y;
            int h = hit_test(w, lx, ly, edges);
            if (h == H_MENUBAR && !popups.empty()) menubar_hover(w, lx - w->border());
            if (h == H_CLIENT) {
                Rect cr = w->client_rect();
                int cx = lx - cr.x, cy = ly - cr.y;
                Widget* wd = widget_at(w, cx, cy);
                if (wd) wd->mouse_move(make_ev(cx - wd->r.x, cy - wd->r.y, 0, 0));
                else w->on_mouse_move(make_ev(cx, cy, 0, 0));
            }
        }
        break;
    }
    }
    update_hover_cursor();
}

void handle_down(int button) {
    uint64_t now = pit::ticks();
    int clicks = 1;
    if (button == last_click_btn && now - last_click_time < 500 && abs_val(mx - last_click_x) < 5 &&
        abs_val(my - last_click_y) < 5) {
        clicks = 2;
        last_click_time = 0;
    } else {
        last_click_time = now;
    }
    last_click_x = mx;
    last_click_y = my;
    last_click_btn = button;

    Window* w = window_at(mx, my);
    // Clic fuera de los menús emergentes: se cierran
    if (!popups.empty() && (!w || !(w->flags & WF_POPUP))) {
        bool on_menubar = false;
        if (w && w->menubar) {
            int e;
            on_menubar = hit_test(w, mx - w->frame.x, my - w->frame.y, e) == H_MENUBAR;
        }
        wm::close_popups();
        if (!on_menubar && w != taskbar) return;
    }
    if (!w) return;
    if (blocked_by_modal(w)) {
        wm::activate(modal_stack.back());
        return;
    }
    if (!(w->flags & WF_POPUP) && w != taskbar) wm::activate(w);

    int lx = mx - w->frame.x, ly = my - w->frame.y, edges;
    int h = hit_test(w, lx, ly, edges);
    drag_win = w;
    switch (h) {
    case H_CLIENT: {
        Rect cr = w->client_rect();
        int cx = lx - cr.x, cy = ly - cr.y;
        Widget* wd = widget_at(w, cx, cy);
        if (wd) {
            if (!wd->enabled) { drag = Drag::NONE; return; }
            if (wd->focusable) w->set_focus(wd);
            drag = Drag::WIDGET;
            drag_widget = wd;
            wd->mouse_down(make_ev(cx - wd->r.x, cy - wd->r.y, button, clicks));
        } else {
            drag = Drag::CLIENT;
            w->on_mouse_down(make_ev(cx, cy, button, clicks));
        }
        break;
    }
    case H_TITLE:
        if (button != MB_LEFT) break;
        if (clicks == 2 && (w->flags & WF_MAXIMIZE)) { wm::toggle_maximize(w); break; }
        if (!w->maximized) {
            drag = Drag::MOVE;
            drag_ox = mx - w->frame.x;
            drag_oy = my - w->frame.y;
        }
        break;
    case H_CLOSE: case H_MAX: case H_MIN:
        if (button != MB_LEFT) break;
        drag = Drag::CAPBTN;
        capbtn = h == H_CLOSE ? 1 : (h == H_MAX ? 2 : 3);
        capbtn_hover = true;
        w->invalidate();
        break;
    case H_BORDER:
        if (button != MB_LEFT) break;
        drag = Drag::RESIZE;
        resize_edges = edges;
        drag_start = w->frame;
        drag_smx = mx;
        drag_smy = my;
        break;
    case H_MENUBAR:
        if (button == MB_LEFT) menubar_click(w, lx - w->border(), ly);
        break;
    }
}

void handle_up(int button) {
    if (mbuttons) {
        // aún hay botones pulsados: sólo informa al receptor actual
        if (drag == Drag::WIDGET) {
            Rect cr = drag_win->client_rect();
            drag_widget->mouse_up(make_ev(mx - drag_win->frame.x - cr.x - drag_widget->r.x,
                                          my - drag_win->frame.y - cr.y - drag_widget->r.y, button, 1));
        }
        return;
    }
    Drag d = drag;
    drag = Drag::NONE;
    switch (d) {
    case Drag::WIDGET: {
        Rect cr = drag_win->client_rect();
        Widget* wd = drag_widget;
        drag_widget = nullptr;
        wd->mouse_up(make_ev(mx - drag_win->frame.x - cr.x - wd->r.x, my - drag_win->frame.y - cr.y - wd->r.y, button, 1));
        break;
    }
    case Drag::CLIENT: {
        Rect cr = drag_win->client_rect();
        drag_win->on_mouse_up(make_ev(mx - drag_win->frame.x - cr.x, my - drag_win->frame.y - cr.y, button, 1));
        break;
    }
    case Drag::CAPBTN: {
        Window* w = drag_win;
        w->invalidate();
        if (capbtn_hover) {
            if (capbtn == 1) w->close();
            else if (capbtn == 2) wm::toggle_maximize(w);
            else wm::minimize(w);
        }
        break;
    }
    default: break;
    }
    update_hover_cursor();
}

void handle_key(const InputEvent& e) {
    uint8_t m = e.mods;
    if (hotkey_handler && hotkey_handler(e)) return;
    if (e.pressed && e.key == KEY_DELETE && (m & MOD_CTRL) && (m & MOD_ALT)) {
        apps::close_program_dialog();   // Ctrl+Alt+Supr
        return;
    }
    if (e.pressed) {
        if ((e.key == KEY_LWIN || e.key == KEY_RWIN || (e.key == KEY_ESC && (m & MOD_CTRL))) && start_handler) {
            if (modal_stack.empty()) start_handler();
            return;
        }
        if (e.key == KEY_F4 && (m & MOD_ALT)) {
            if (!popups.empty()) { wm::close_popups(); return; }
            Window* a = active_win;
            if (a && !(a->flags & (WF_DESKTOP | WF_NOCLOSE)) && a != taskbar) a->close();
            return;
        }
        if (e.key == KEY_TAB && (m & MOD_ALT)) {
            if (!modal_stack.empty()) return;
            // activa la ventana normal más baja (rotación)
            for (Window* w : wins) {
                if (w != active_win && !(w->flags & (WF_DESKTOP | WF_POPUP | WF_NO_TASKBAR)) && w != taskbar) {
                    wm::activate(w);
                    break;
                }
            }
            return;
        }
    }
    if (!popups.empty()) {
        popups.back()->on_key(e);
        return;
    }
    Window* w = active_win;
    if (!w) return;
    if (w->focus && w->focus->visible && w->focus->enabled && w->focus->key(e)) return;
    if (w->on_key(e)) return;
    // Alt+letra abre el menú correspondiente
    if (e.pressed && (m & MOD_ALT) && w->menubar && e.key < 256) {
        if (menubar_mnemonic(w, (char)e.key)) return;
    }
    if (e.pressed && e.key == KEY_TAB && w->widgets.size()) {
        // Tab: siguiente control enfocable
        int n = (int)w->widgets.size();
        int start = w->focus ? w->widgets.index_of(w->focus) : -1;
        int dir = (m & MOD_SHIFT) ? -1 : 1;
        for (int i = 1; i <= n; i++) {
            Widget* c = w->widgets[(size_t)((start + dir * i + n * 2) % n)];
            if (c->focusable && c->visible && c->enabled) { w->set_focus(c); break; }
        }
    }
}

void process_input() {
    InputEvent e;
    while (input::pop(e)) {
        if (e.type == InputEvent::MOUSE) {
            // Ganancia en dieciseisavos según la velocidad elegida (lenta 1/2, normal 3/4,
            // rápida 1, muy rápida 3/2) y aceleración suave sólo en movimientos amplios.
            static const int gain16[5] = {12, 8, 12, 16, 24};
            static int rem_x = 0, rem_y = 0;
            int g = gain16[clamp(mouse_speed, 1, 4)];
            int mag = abs_val((int)e.dx) + abs_val((int)e.dy);
            if (mag > 24) g = g * 3 / 2;
            rem_x += e.dx * g;
            rem_y += e.dy * g;
            int dx = rem_x / 16, dy = rem_y / 16;
            rem_x -= dx * 16;
            rem_y -= dy * 16;
            int nx = clamp(mx + dx, 0, wm::screen_w() - 1);
            int ny = clamp(my + dy, 0, wm::screen_h() - 1);
            if (nx != mx || ny != my) {
                wm::dirty(cursor_rect());
                mx = nx;
                my = ny;
                wm::dirty(cursor_rect());
                handle_move();
            }
            if (e.wheel) {
                Window* w = drag != Drag::NONE ? drag_win : window_at(mx, my);
                if (w && !blocked_by_modal(w)) {
                    Rect cr = w->client_rect();
                    int cx = mx - w->frame.x - cr.x, cy = my - w->frame.y - cr.y;
                    MouseEv ev = make_ev(cx, cy, 0, 0);
                    ev.wheel = e.wheel;
                    Widget* wd = widget_at(w, cx, cy);
                    if (!wd && w->focus) wd = w->focus;
                    if (wd) { ev.x -= wd->r.x; ev.y -= wd->r.y; wd->mouse_wheel(ev); }
                    else w->on_mouse_wheel(ev);
                }
            }
            uint8_t pressed = e.buttons & ~mbuttons;
            uint8_t released = mbuttons & ~e.buttons;
            for (int b = 1; b <= 4; b <<= 1) {
                if (pressed & b) { mbuttons |= b; handle_down(b); }
                if (released & b) { mbuttons &= ~b; handle_up(b); }
            }
        } else if (e.type == InputEvent::KEY) {
            handle_key(e);
        }
    }
}

void run_timers() {
    uint64_t now = pit::ticks();
    for (size_t i = 0; i < timers.size(); i++) {
        Timer& t = timers[i];
        if (now >= t.next) {
            t.next = now + t.interval;
            Window* w = t.w;
            int id = t.id;
            w->on_timer(id);
        }
    }
    if (now >= next_tick) {
        next_tick = now + 500;
        if (active_win && active_win->focus) active_win->focus->tick();
    }
}

void compose() {
    // Repinta las ventanas marcadas
    for (size_t i = 0; i < wins.size(); i++) {
        Window* w = wins[i];
        if (w->dirty && w->visible) {
            w->render();
            if (!w->minimized) wm::dirty(w->frame);
        }
    }
    if (!ndirty) return;
    Surface& bb = gfx::backbuffer();
    for (int d = 0; d < ndirty; d++) {
        Rect r = dirty_list[d].intersect({0, 0, bb.w, bb.h});
        if (r.empty()) continue;
        bb.reset_clip();
        bb.set_clip(r);
        for (Window* w : wins) {
            if (!w->visible || w->minimized || !w->frame.intersects(r)) continue;
            bb.blit(w->surf, w->frame.x, w->frame.y);
        }
        if (cursor_rect().intersects(r)) draw_cursor(bb);
        gfx::present(r);
    }
    bb.reset_clip();
    ndirty = 0;
}

void reap() {
    for (Window* w : graveyard) delete w;
    graveyard.clear();
}

void loop_once() {
    usb::poll();
    process_input();
    ps2::poll();
    run_timers();
    compose();
}
}

// ---------------------------------------------------------------------------
// Window
// ---------------------------------------------------------------------------
Window::Window(const char* t, int cw, int ch, uint32_t f) : flags(f) {
    strlcpy(title, t, sizeof title);
    int b = border();
    int th = (flags & WF_TITLE) ? TITLE_H + 1 : 0;
    frame = {0, 0, cw + 2 * b, ch + 2 * b + th};
    // Posición en cascada
    if (!(flags & (WF_POPUP | WF_DESKTOP))) {
        frame.x = 60 + cascade * 26;
        frame.y = 40 + cascade * 26;
        cascade = (cascade + 1) % 8;
        if (frame.right() > wm::screen_w()) frame.x = max(0, wm::screen_w() - frame.w);
        if (frame.bottom() > wm::screen_h() - TASKBAR_H) frame.y = max(0, wm::screen_h() - TASKBAR_H - frame.h);
    }
}

Window::~Window() {
    for (Widget* w : widgets) delete w;
    delete_menubar(menubar);
}

int Window::border() const { return (flags & (WF_POPUP | WF_NOBORDER)) ? 0 : BORDER; }

Rect Window::client_rect() const {
    int b = border();
    int top = b + ((flags & WF_TITLE) ? TITLE_H + 1 : 0) + (menubar ? MENUBAR_H : 0);
    return {b, top, frame.w - 2 * b, frame.h - top - b};
}

void Window::set_title(const char* t) {
    strlcpy(title, t, sizeof title);
    invalidate();
    if (taskbar) taskbar->invalidate();
}

void Window::center(Window* rel) {
    Rect area = rel ? rel->frame : wm::work_area();
    move_to(area.x + (area.w - frame.w) / 2, area.y + (area.h - frame.h) / 2);
}

void Window::move_to(int x, int y) {
    if (visible) wm::dirty(frame);
    frame.x = clamp(x, 0, max(0, wm::screen_w() - frame.w));
    frame.y = clamp(y, 0, max(0, wm::screen_h() - TASKBAR_H - frame.h));
    if (visible) wm::dirty(frame);
}

void Window::resize_client(int cw, int ch) {
    Rect cr = client_rect();
    if (visible) wm::dirty(frame);
    frame.w += cw - cr.w;
    frame.h += ch - cr.h;
    on_resize();
    invalidate();
}

void Window::set_focus(Widget* w) {
    if (focus == w) return;
    Widget* old = focus;
    focus = w;
    if (old) { old->focus_changed(false); old->invalidate(); }
    if (w) { w->focus_changed(true); w->invalidate(); }
}

void Window::close() {
    if (closing) return;
    if (on_close()) wm::destroy(this);
}

static void draw_cap_glyph(Surface& s, const Rect& r, int kind, bool restore_icon, bool pressed) {
    if (pressed) s.bevel_pressed(r); else s.bevel_raised(r);
    s.fill(r.inset(2), col::FACE);
    int o = pressed ? 1 : 0;
    int x = r.x + 4 + o, y = r.y + 3 + o;
    if (kind == 1) {
        for (int i = 0; i < 7; i++) {
            s.put(x + i, y + i, col::BLACK); s.put(x + i + 1, y + i, col::BLACK);
            s.put(x + 6 - i, y + i, col::BLACK); s.put(x + 7 - i, y + i, col::BLACK);
        }
    } else if (kind == 2 && !restore_icon) {
        s.rect({x - 1, y - 1, 9, 9}, col::BLACK);
        s.hline(x - 1, y, 9, col::BLACK);
    } else if (kind == 2) {
        s.rect({x + 1, y - 1, 6, 6}, col::BLACK);
        s.hline(x + 1, y, 6, col::BLACK);
        s.fill({x - 1, y + 2, 6, 6}, col::FACE);
        s.rect({x - 1, y + 2, 6, 6}, col::BLACK);
        s.hline(x - 1, y + 3, 6, col::BLACK);
    } else {
        s.fill({x, y + 6, 6, 2}, col::BLACK);
    }
}

void Window::paint_frame(bool active) {
    Surface& s = surf;
    s.reset_clip();
    if (flags & (WF_POPUP | WF_NOBORDER)) return;
    Rect r{0, 0, frame.w, frame.h};
    s.fill(r, col::FACE);
    s.bevel_raised(r);
    if (!(flags & WF_TITLE)) return;
    int b = border();
    Rect t{b, b, frame.w - 2 * b, TITLE_H};
    if (active) s.gradient_h(t, col::TITLE_A1, col::TITLE_A2);
    else s.gradient_h(t, col::TITLE_I1, col::TITLE_I2);
    int tx = t.x + 3;
    if (icon && !(flags & WF_DIALOG)) {
        draw_icon(s, icon->small, t.x + 1, t.y + 1);
        tx = t.x + 20;
    }
    // Título recortado con puntos suspensivos si no cabe
    Rect last_btn = cap_rect(this, 3);
    if (last_btn.empty()) last_btn = cap_rect(this, 2);
    if (last_btn.empty()) last_btn = cap_rect(this, 1);
    int avail = last_btn.x - 4 - tx;
    const Font& f = font_ui_bold;
    int len = (int)strlen(title);
    uint32_t tc = active ? col::WHITE : col::FACE;
    if (f.text_width(title) <= avail) {
        s.text(f, tx, t.y + (TITLE_H - f.height) / 2, title, tc);
    } else {
        int dots = f.text_width("...");
        while (len > 0 && f.text_width(title, len) + dots > avail) len--;
        int w = s.text(f, tx, t.y + (TITLE_H - f.height) / 2, title, tc, len);
        if (len > 0) s.text(f, tx + w, t.y + (TITLE_H - f.height) / 2, "...", tc);
    }
    bool pressed_any = drag == Drag::CAPBTN && drag_win == this && capbtn_hover;
    if (!(flags & WF_NOCLOSE)) draw_cap_glyph(s, cap_rect(this, 1), 1, false, pressed_any && capbtn == 1);
    if (flags & WF_MAXIMIZE) draw_cap_glyph(s, cap_rect(this, 2), 2, maximized, pressed_any && capbtn == 2);
    if (flags & WF_MINIMIZE) draw_cap_glyph(s, cap_rect(this, 3), 3, false, pressed_any && capbtn == 3);
}

void Window::render() {
    if (surf.w != frame.w || surf.h != frame.h || !surf.px) surf.resize(frame.w, frame.h);
    bool act = active_win == this || (popups.size() && owner == active_win && (flags & WF_POPUP));
    paint_frame(act);
    Rect cr = client_rect();
    if (menubar) {
        PaintScope ps(surf, {cr.x, cr.y - MENUBAR_H, cr.w, MENUBAR_H});
        paint_menubar(this, surf, cr.w);
    }
    {
        PaintScope ps(surf, cr);
        on_paint(surf);
        for (Widget* w : widgets) {
            if (!w->visible) continue;
            PaintScope pw(surf, w->r);
            w->paint(surf);
        }
    }
    surf.reset_clip();
    dirty = false;
}

// ---------------------------------------------------------------------------
// Widget
// ---------------------------------------------------------------------------
bool Widget::has_focus() const { return win && win->focus == this && active_win == win; }
void Widget::invalidate() { if (win) win->invalidate(); }
void Widget::notify(int code) { if (win) win->on_command(id, code); }
void Widget::focus() { if (win) win->set_focus(this); }

// ---------------------------------------------------------------------------
// API del gestor
// ---------------------------------------------------------------------------
void wm::init() {
    mx = screen_w() / 2;
    my = screen_h() / 2;
}

int wm::screen_w() { return gfx::width(); }
int wm::screen_h() { return gfx::height(); }
Rect wm::work_area() { return {0, 0, screen_w(), screen_h() - (taskbar ? TASKBAR_H : 0)}; }
Window* wm::active() { return active_win; }
Vec<Window*>& wm::windows() { return wins; }
String& wm::clipboard() { return clip; }
void wm::set_taskbar(Window* tb) { taskbar = tb; }
void wm::set_start_handler(void (*fn)()) { start_handler = fn; }
void wm::set_hotkey_handler(bool (*fn)(const InputEvent&)) { hotkey_handler = fn; }

void Window::retranslate() {
    set_title(i18n::retranslate(title));
    for (Widget* w : widgets) w->retranslate();
    if (menubar) menubar->retranslate();
    invalidate();
}

void wm::retranslate_all() {
    Menu::retranslate_all();
    for (Window* w : wins) w->retranslate();
    dirty({0, 0, screen_w(), screen_h()});
}
void wm::set_mouse_speed(int s) { mouse_speed = clamp(s, 1, 4); }

void wm::dirty(const Rect& r0) {
    Rect r = r0.intersect({0, 0, screen_w(), screen_h()});
    if (r.empty()) return;
    for (int i = 0; i < ndirty; i++) {
        if (dirty_list[i].intersects(r)) {
            dirty_list[i] = dirty_list[i].unite(r);
            return;
        }
    }
    if (ndirty == MAX_DIRTY) {
        for (int i = 1; i < ndirty; i++) dirty_list[0] = dirty_list[0].unite(dirty_list[i]);
        ndirty = 1;
        dirty_list[0] = dirty_list[0].unite(r);
        return;
    }
    dirty_list[ndirty++] = r;
}

void wm::add(Window* w) {
    if (w->visible) return;
    w->visible = true;
    w->dirty = true;
    if (w->flags & WF_DESKTOP) wins.insert(0, w);
    else raise(w);
    w->on_resize();
    if (w->focus) w->focus->focus_changed(true);
    if (!(w->flags & (WF_POPUP | WF_DESKTOP)) && w != taskbar) activate(w);
    if (taskbar) taskbar->invalidate();
}

void wm::activate(Window* w) {
    if (!w || (w->flags & WF_POPUP)) return;
    if (w->minimized) restore(w);
    raise(w);
    dirty(w->frame);
    if (active_win == w) return;
    Window* old = active_win;
    active_win = w;
    if (old) { old->invalidate(); old->on_activate(false); }
    w->invalidate();
    w->on_activate(true);
    if (taskbar) taskbar->invalidate();
}

static void activate_next(Window* except) {
    for (size_t i = wins.size(); i > 0; i--) {
        Window* c = wins[i - 1];
        if (c != except && c->visible && !c->minimized && !(c->flags & WF_POPUP) && c != taskbar) {
            wm::activate(c);
            return;
        }
    }
    active_win = nullptr;
}

void wm::minimize(Window* w) {
    if (w->minimized) return;
    w->minimized = true;
    dirty(w->frame);
    if (active_win == w) activate_next(w);
    if (taskbar) taskbar->invalidate();
}

void wm::restore(Window* w) {
    if (!w->minimized) return;
    w->minimized = false;
    w->invalidate();
    dirty(w->frame);
    if (taskbar) taskbar->invalidate();
}

void wm::toggle_maximize(Window* w) {
    if (!(w->flags & WF_MAXIMIZE)) return;
    dirty(w->frame);
    if (w->maximized) {
        w->frame = w->restore_frame;
        w->maximized = false;
    } else {
        w->restore_frame = w->frame;
        Rect wa = work_area();
        w->frame = wa;
        w->maximized = true;
    }
    w->on_resize();
    w->invalidate();
}

void wm::destroy(Window* w) {
    if (w->closing) return;
    w->closing = true;
    w->on_destroyed();
    // Cierra primero los diálogos y menús que dependen de ella
    for (size_t i = wins.size(); i > 0; i--) {
        if (i - 1 < wins.size() && wins[i - 1]->owner == w) destroy(wins[i - 1]);
    }
    popups.remove(w);
    wins.remove(w);
    modal_stack.remove(w);
    for (size_t i = timers.size(); i > 0; i--)
        if (timers[i - 1].w == w) timers.erase(i - 1);
    if (drag_win == w) { drag = Drag::NONE; drag_win = nullptr; drag_widget = nullptr; }
    if (w->visible) dirty(w->frame);
    w->visible = false;
    if (active_win == w) {
        active_win = nullptr;
        Window* o = w->owner;
        while (o && o->closing) o = o->owner;
        if (o && !(o->flags & WF_POPUP)) activate(o);
        else activate_next(w);
    }
    if (taskbar) taskbar->invalidate();
    graveyard.push(w);
}

void wm::set_timer(Window* w, int id, uint32_t interval) {
    kill_timer(w, id);
    timers.push({w, id, interval, pit::ticks() + interval});
}

void wm::kill_timer(Window* w, int id) {
    for (size_t i = 0; i < timers.size(); i++)
        if (timers[i].w == w && timers[i].id == id) { timers.erase(i); return; }
}

void wm::open_popup(Window* p) {
    popups.push(p);
    p->visible = false;
    add(p);
}

void wm::close_popups(Window* from) {
    while (!popups.empty()) {
        Window* p = popups.back();
        destroy(p);
        if (p == from) break;
    }
}

bool wm::popup_open() { return !popups.empty(); }

void wm::set_busy(bool b) {
    if (b == busy) return;
    dirty(cursor_rect());
    busy = b;
    dirty(cursor_rect());
    compose();
}

void wm::pump() {
    usb::poll();
    process_input();
    run_timers();
    compose();
}

int wm::run_modal(Window* w) {
    if (!w->visible) add(w);
    modal_stack.push(w);
    while (!w->closing) {
        loop_once();
        if (!ndirty) sched::idle();   // los programas .run siguen funcionando
    }
    int r = w->modal_result;
    return r;
}

void wm::run() {
    for (;;) {
        loop_once();
        reap();
        process::reap();     // programas .run que han terminado o fallado
        sched::idle();       // sin trabajo: el procesador pasa a los programas (o se espera)
    }
}

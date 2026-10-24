// Buscaminas
#include "apps/common.h"
#include "arch/idt.h"

namespace {
enum { M_NEW = 100, M_BEG, M_INT, M_EXP, M_EXIT, M_ABOUT };
constexpr int CELL = 16, MARGIN = 12, HEADER = 44;

class Mines : public Window {
public:
    int cols = 9, rows = 9, mines = 10;
    Vec<uint8_t> mine, state, count;   // state: 0 tapada, 1 descubierta, 2 bandera, 3 interrogación
    bool started = false, over = false, won = false;
    int flags = 0, seconds = 0, revealed = 0;
    int press_x = -1, press_y = -1;
    bool pressing_face = false, face_down = false, chord = false;
    int boom = -1;
    uint32_t rng;
    Menu* game_menu;

    Mines() : Window(T("Buscaminas"), 100, 100, WF_FIXED) {
        icon = &icon_mine;
        auto* mb = new MenuBar();
        game_menu = mb->add(T("&Juego"));
        game_menu->add(T("&Nuevo"), M_NEW, "F2").separator().add(T("&Principiante"), M_BEG).add(T("&Intermedio"), M_INT)
            .add(T("&Experto"), M_EXP).separator().add(T("&Salir"), M_EXIT);
        mb->add(T("Ay&uda"))->add(T("&Acerca de Buscaminas"), M_ABOUT);
        attach_menubar(this, mb);
        rng = (uint32_t)pit::ticks() * 2654435761u + 1;
        set_level(9, 9, 10);
    }

    void set_level(int c, int r, int m) {
        cols = c; rows = r; mines = m;
        resize_client(cols * CELL + 2 * MARGIN, rows * CELL + HEADER + MARGIN * 2);
        reset();
    }

    void reset() {
        int n = cols * rows;
        mine.resize(0); mine.resize((size_t)n);
        state.resize(0); state.resize((size_t)n);
        count.resize(0); count.resize((size_t)n);
        for (int i = 0; i < n; i++) { mine[i] = 0; state[i] = 0; count[i] = 0; }
        started = over = won = false;
        flags = seconds = revealed = 0;
        boom = -1;
        wm::kill_timer(this, 1);
        invalidate();
    }

    uint32_t rand() { rng = rng * 1103515245 + 12345; return rng >> 8; }

    void place(int safe) {
        int n = cols * rows;
        int placed = 0;
        int sx = safe % cols, sy = safe / cols;
        while (placed < mines) {
            int i = (int)(rand() % (uint32_t)n);
            int x = i % cols, y = i / cols;
            if (mine[i] || (abs_val(x - sx) <= 1 && abs_val(y - sy) <= 1 && n - 9 >= mines)) continue;
            mine[i] = 1;
            placed++;
        }
        for (int y = 0; y < rows; y++)
            for (int x = 0; x < cols; x++) {
                int c = 0;
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++) {
                        int nx = x + dx, ny = y + dy;
                        if (nx >= 0 && ny >= 0 && nx < cols && ny < rows && mine[ny * cols + nx]) c++;
                    }
                count[y * cols + x] = (uint8_t)c;
            }
    }

    void reveal(int x, int y) {
        // relleno iterativo de las zonas vacías
        Vec<int> st;
        st.push(y * cols + x);
        while (!st.empty()) {
            int i = st.back();
            st.pop();
            if (state[i] == 1 || state[i] == 2) continue;
            state[i] = 1;
            revealed++;
            if (mine[i]) { lose(i); return; }
            if (count[i]) continue;
            int cx = i % cols, cy = i / cols;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    int nx = cx + dx, ny = cy + dy;
                    if (nx >= 0 && ny >= 0 && nx < cols && ny < rows && state[ny * cols + nx] != 1) st.push(ny * cols + nx);
                }
        }
        if (revealed == cols * rows - mines) win();
    }

    void lose(int i) {
        over = true;
        boom = i;
        wm::kill_timer(this, 1);
    }

    void win() {
        over = true;
        won = true;
        wm::kill_timer(this, 1);
        for (int i = 0; i < cols * rows; i++) if (mine[i]) state[i] = 2;
        flags = mines;
        invalidate();
    }

    void open_cell(int x, int y) {
        int i = y * cols + x;
        if (state[i] == 2) return;
        if (!started) {
            place(i);
            started = true;
            seconds = 1;
            wm::set_timer(this, 1, 1000);
        }
        reveal(x, y);
    }

    void chord_cell(int x, int y) {
        int i = y * cols + x;
        if (state[i] != 1 || !count[i]) return;
        int f = 0;
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                int nx = x + dx, ny = y + dy;
                if (nx >= 0 && ny >= 0 && nx < cols && ny < rows && state[ny * cols + nx] == 2) f++;
            }
        if (f != count[i]) return;
        for (int dy = -1; dy <= 1 && !over; dy++)
            for (int dx = -1; dx <= 1 && !over; dx++) {
                int nx = x + dx, ny = y + dy;
                if (nx >= 0 && ny >= 0 && nx < cols && ny < rows && state[ny * cols + nx] == 0) reveal(nx, ny);
            }
    }

    Rect face_rect() const { return {client_w() / 2 - 13, MARGIN + 8, 26, 26}; }
    Rect field_rect() const { return {MARGIN, MARGIN + HEADER, cols * CELL, rows * CELL}; }

    void draw_digit(Surface& s, int x, int y, int d) {
        // segmentos: a b c d e f g
        static const uint8_t seg[11] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F, 0x40};
        uint8_t m = seg[d];
        uint32_t on = 0xFF0000, off = 0x500000;
        auto H = [&](int yy, bool lit) { s.fill({x + 2, y + yy, 9, 2}, lit ? on : off); };
        auto V = [&](int xx, int yy, bool lit) { s.fill({x + xx, y + yy, 2, 9}, lit ? on : off); };
        H(0, m & 1); V(10, 1, m & 2); V(10, 12, m & 4); H(21, m & 8); V(1, 12, m & 16); V(1, 1, m & 32); H(10, m & 64);
    }

    void draw_counter(Surface& s, int x, int y, int v) {
        s.fill({x, y, 41, 25}, col::BLACK);
        s.bevel_thin_sunken({x - 1, y - 1, 43, 27});
        bool negv = v < 0;
        v = clamp(abs_val(v), 0, 999);
        int d[3] = {v / 100, (v / 10) % 10, v % 10};
        if (negv) d[0] = 10;
        for (int i = 0; i < 3; i++) draw_digit(s, x + 1 + i * 13, y + 1, d[i]);
    }

    void draw_face(Surface& s) {
        Rect f = face_rect();
        if (face_down) s.bevel_pressed(f); else s.bevel_raised(f);
        int o = face_down ? 1 : 0;
        int cx = f.x + 13 + o, cy = f.y + 13 + o;
        s.ellipse({cx - 8, cy - 8, 17, 17}, col::YELLOW, true);
        s.ellipse({cx - 8, cy - 8, 17, 17}, col::BLACK, false);
        if (over && !won) {
            for (int k = -1; k <= 1; k++) { s.put(cx - 4 + k, cy - 3 + k, 0); s.put(cx - 4 + k, cy - 3 - k, 0); s.put(cx + 3 + k, cy - 3 + k, 0); s.put(cx + 3 + k, cy - 3 - k, 0); }
            s.hline(cx - 3, cy + 4, 7, 0); s.put(cx - 4, cy + 5, 0); s.put(cx + 4, cy + 5, 0);
        } else if (won) {
            s.fill({cx - 6, cy - 4, 5, 3}, 0); s.fill({cx + 1, cy - 4, 5, 3}, 0); s.hline(cx - 7, cy - 4, 15, 0);
            s.hline(cx - 3, cy + 4, 7, 0); s.put(cx - 4, cy + 3, 0); s.put(cx + 4, cy + 3, 0);
        } else {
            s.fill({cx - 4, cy - 3, 2, 2}, 0); s.fill({cx + 3, cy - 3, 2, 2}, 0);
            if (press_x >= 0) s.ellipse({cx - 2, cy + 2, 5, 5}, 0, false);
            else { s.hline(cx - 3, cy + 4, 7, 0); s.put(cx - 4, cy + 3, 0); s.put(cx + 4, cy + 3, 0); }
        }
    }

    void on_paint(Surface& s) override {
        int w = client_w(), h = client_h();
        s.clear(col::FACE);
        // marco exterior y paneles hundidos (borde de 3 píxeles)
        for (int i = 0; i < 3; i++) {
            s.hline(i, i, w - 2 * i, col::HILIGHT);
            s.vline(i, i, h - 2 * i, col::HILIGHT);
        }
        Rect hdr{MARGIN - 3, MARGIN - 3, w - 2 * MARGIN + 6, HEADER - 6};
        for (int i = 0; i < 2; i++) s.bevel_thin_sunken(hdr.inset(i));
        draw_counter(s, MARGIN + 4, MARGIN + 9, mines - flags);
        draw_counter(s, w - MARGIN - 46, MARGIN + 9, seconds);
        draw_face(s);
        Rect fr = field_rect();
        for (int i = 0; i < 3; i++) s.bevel_thin_sunken(Rect{fr.x - 3 + i, fr.y - 3 + i, fr.w + 6 - 2 * i, fr.h + 6 - 2 * i});
        static const uint32_t numcol[9] = {0, 0x0000FF, 0x008000, 0xFF0000, 0x000080, 0x800000, 0x008080, 0x000000, 0x808080};
        for (int y = 0; y < rows; y++)
            for (int x = 0; x < cols; x++) {
                int i = y * cols + x;
                Rect c{fr.x + x * CELL, fr.y + y * CELL, CELL, CELL};
                bool pressed_look = false;
                if (press_x >= 0 && state[i] == 0 && !over) {
                    if (chord) pressed_look = abs_val(x - press_x) <= 1 && abs_val(y - press_y) <= 1;
                    else pressed_look = x == press_x && y == press_y;
                }
                bool show_mine = over && mine[i] && state[i] != 2;
                if (state[i] == 1 || pressed_look || show_mine) {
                    s.fill(c, i == boom ? col::RED : col::FACE);
                    s.hline(c.x, c.y, CELL, col::SHADOW);
                    s.vline(c.x, c.y, CELL, col::SHADOW);
                    if (show_mine) {
                        int mx = c.x + 8, my = c.y + 8;
                        s.ellipse({mx - 5, my - 5, 11, 11}, 0, true);
                        s.hline(mx - 7, my, 15, 0); s.vline(mx, my - 7, 15, 0);
                        s.put(mx - 5, my - 5, 0); s.put(mx + 5, my - 5, 0); s.put(mx - 5, my + 5, 0); s.put(mx + 5, my + 5, 0);
                        s.fill({mx - 2, my - 2, 2, 2}, col::WHITE);
                    } else if (state[i] == 1 && count[i]) {
                        char t[2] = {(char)('0' + count[i]), 0};
                        s.text_centered(font_ui_bold, c, t, numcol[count[i]]);
                    }
                } else {
                    s.fill(c, col::FACE);
                    s.bevel_raised(c);
                    if (state[i] == 2 || state[i] == 3) {
                        if (state[i] == 3) { s.text_centered(font_ui_bold, c, "?", col::BLACK); continue; }
                        // bandera
                        s.fill({c.x + 5, c.y + 3, 5, 5}, col::RED);
                        s.vline(c.x + 9, c.y + 3, 8, col::BLACK);
                        s.fill({c.x + 6, c.y + 11, 6, 1}, col::BLACK);
                        s.fill({c.x + 4, c.y + 12, 10, 2}, col::BLACK);
                        if (over && !mine[i]) {
                            s.line(c.x + 3, c.y + 3, c.x + 12, c.y + 12, col::RED);
                            s.line(c.x + 12, c.y + 3, c.x + 3, c.y + 12, col::RED);
                        }
                    }
                }
            }
    }

    bool cell_at(int mx, int my, int& x, int& y) {
        Rect fr = field_rect();
        if (!fr.contains(mx, my)) return false;
        x = (mx - fr.x) / CELL;
        y = (my - fr.y) / CELL;
        return true;
    }

    void on_mouse_down(const MouseEv& e) override {
        if (face_rect().contains(e.x, e.y) && e.button == MB_LEFT) { pressing_face = face_down = true; invalidate(); return; }
        if (over) return;
        int x, y;
        if (!cell_at(e.x, e.y, x, y)) return;
        if (e.button == MB_RIGHT && !(e.buttons & MB_LEFT)) {
            int i = y * cols + x;
            if (state[i] == 0) { state[i] = 2; flags++; }
            else if (state[i] == 2) { state[i] = 3; flags--; }
            else if (state[i] == 3) state[i] = 0;
            invalidate();
            return;
        }
        chord = (e.buttons & (MB_LEFT | MB_RIGHT)) == (MB_LEFT | MB_RIGHT) || e.button == MB_MIDDLE || e.clicks == 2;
        press_x = x;
        press_y = y;
        invalidate();
    }

    void on_mouse_move(const MouseEv& e) override {
        if (pressing_face) {
            bool d = face_rect().contains(e.x, e.y);
            if (d != face_down) { face_down = d; invalidate(); }
            return;
        }
        if (press_x < 0) return;
        int x, y;
        if (cell_at(e.x, e.y, x, y)) { if (x != press_x || y != press_y) { press_x = x; press_y = y; invalidate(); } }
        else if (press_x != -2) { press_x = -2; invalidate(); }
    }

    void on_mouse_up(const MouseEv& e) override {
        if (pressing_face) {
            pressing_face = false;
            if (face_down) reset();
            face_down = false;
            invalidate();
            return;
        }
        if (press_x == -1) return;
        int x, y;
        bool in = cell_at(e.x, e.y, x, y);
        bool was_chord = chord;
        press_x = press_y = -1;
        chord = false;
        if (in && !over) {
            if (was_chord) chord_cell(x, y);
            else open_cell(x, y);
        }
        invalidate();
    }

    void on_timer(int) override {
        if (started && !over && seconds < 999) { seconds++; invalidate(); }
    }

    void on_menu_init(Menu* m) override {
        m->check(M_BEG, cols == 9 && rows == 9);
        m->check(M_INT, cols == 16 && rows == 16);
        m->check(M_EXP, cols == 30);
    }

    void on_command(int id, int) override {
        switch (id) {
        case M_NEW: reset(); break;
        case M_BEG: set_level(9, 9, 10); break;
        case M_INT: set_level(16, 16, 40); break;
        case M_EXP: set_level(30, 16, 99); break;
        case M_EXIT: close(); break;
        case M_ABOUT: msgbox(this, T("Acerca de Buscaminas"), T("Buscaminas de BrysOS\nVersión 1.0\n\nDescubra todas las casillas sin minas."), MB_OK, &icon_mine); break;
        }
    }

    bool on_key(const InputEvent& e) override {
        if (e.pressed && e.key == KEY_F2) { reset(); return true; }
        return false;
    }
};
}

namespace apps {
void minesweeper() {
    wm::add(new Mines());
}
}

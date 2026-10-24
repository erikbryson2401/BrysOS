// Paint: editor de mapas de bits que guarda y abre archivos BMP
#include "apps/common.h"
#include "bsl/loader.h"
#include "bsl/images.h"
#include "apps/bmp.h"
#include "arch/idt.h"

namespace {
enum Tool { T_SELECT_NONE = -1, T_PENCIL, T_BRUSH, T_AIR, T_ERASER, T_FILL, T_PICK, T_LINE, T_RECT, T_ELLIPSE, T_TEXT, T_COUNT };
const char* tool_names[T_COUNT] = {"Lápiz", "Pincel", "Aerógrafo", "Borrador", "Relleno con color", "Seleccionar color",
                                    "Línea", "Rectángulo", "Elipse", "Texto"};
enum {
    M_NEW = 100, M_OPEN, M_SAVE, M_SAVEAS, M_EXIT, M_UNDO, M_CLEAR, M_INVERT, M_FLIPH, M_FLIPV, M_ATTR, M_ABOUT,
};

const uint32_t palette[28] = {
    0x000000, 0x808080, 0x800000, 0x808000, 0x008000, 0x008080, 0x000080, 0x800080, 0x808040, 0x004040, 0x0080FF, 0x004080, 0x8000FF, 0x804000,
    0xFFFFFF, 0xC0C0C0, 0xFF0000, 0xFFFF00, 0x00FF00, 0x00FFFF, 0x0000FF, 0xFF00FF, 0xFFFF80, 0x00FF80, 0x80FFFF, 0x8080FF, 0xFF0080, 0xFF8040,
};

constexpr int TOOLBOX_W = 58, PALETTE_H = 46, STATUS_H = 20;

class PaintWin;

class Canvas : public Widget {
public:
    PaintWin* pw;
    ScrollBar vs, hs;
    int sx = 0, sy = 0;     // desplazamiento
    int sb = 0;
    explicit Canvas(PaintWin* p) : pw(p) { vs.vertical = true; hs.vertical = false; }
    void layout();
    void paint(Surface& s) override;
    void mouse_down(const MouseEv& e) override;
    void mouse_move(const MouseEv& e) override;
    void mouse_up(const MouseEv& e) override;
    void mouse_wheel(const MouseEv& e) override { layout(); vs.set(sy + e.wheel * 40); sy = vs.pos; invalidate(); }
    Cursor cursor(int x, int y) override;
};

class PaintWin : public Window {
public:
    Surface img;
    Surface undo;
    bool has_undo = false;
    Canvas* canvas;
    Label* status;
    String path;
    bool modified = false;
    int tool = T_PENCIL;
    uint32_t fg = 0x000000, bg = 0xFFFFFF;
    int size_opt = 1;       // grosor (índice)
    int fill_mode = 0;      // 0 contorno, 1 contorno+relleno, 2 relleno
    // estado del trazo
    bool drawing = false;
    uint32_t draw_color = 0, other_color = 0;
    int lx = 0, ly = 0, ox = 0, oy = 0;
    uint32_t rng = 12345;

    PaintWin(const char* file) : Window(T("Sin título - Paint"), 640, 460, WF_NORMAL) {
        icon = &icon_paint;
        min_w = 300;
        min_h = 260;
        auto* mb = new MenuBar();
        mb->add(T("&Archivo"))->add(T("&Nuevo"), M_NEW, T("Ctrl+U")).add(T("&Abrir..."), M_OPEN, T("Ctrl+A")).add(T("&Guardar"), M_SAVE, T("Ctrl+G"))
            .add(T("Guardar &como..."), M_SAVEAS).separator().add(T("&Salir"), M_EXIT);
        mb->add(T("&Edición"))->add(T("&Deshacer"), M_UNDO, T("Ctrl+Z")).separator().add(T("&Borrar imagen"), M_CLEAR, T("Ctrl+Mayús+N"));
        mb->add(T("&Imagen"))->add(T("&Voltear horizontalmente"), M_FLIPH).add(T("Voltear &verticalmente"), M_FLIPV)
            .add(T("&Invertir colores"), M_INVERT, T("Ctrl+I")).separator().add(T("&Atributos..."), M_ATTR, T("Ctrl+E"));
        mb->add(T("Ay&uda"))->add(T("&Acerca de Paint"), M_ABOUT);
        attach_menubar(this, mb);
        img.resize(480, 320);
        img.clear(0xFFFFFF);
        canvas = add(new Canvas(this));
        canvas->focusable = true;
        status = add(new Label({0, 0, 10, 10}, ""));
        status->sunken = true;
        focus = canvas;
        if (file) load(file);
    }

    void on_resize() override {
        int w = client_w(), h = client_h();
        canvas->r = {TOOLBOX_W, 0, w - TOOLBOX_W, h - PALETTE_H - STATUS_H};
        status->r = {0, h - STATUS_H, w, STATUS_H};
    }

    Rect tool_rect(int t) const { return {4 + (t % 2) * 25, 4 + (t / 2) * 25, 25, 25}; }
    Rect options_rect() const { return {6, 4 + 5 * 25 + 6, 46, 70}; }
    Rect color_rect(int i) const {
        int y0 = client_h() - STATUS_H - PALETTE_H + 6;
        return {44 + (i % 14) * 16, y0 + (i / 14) * 16, 16, 16};
    }

    void draw_tool_glyph(Surface& s, int t, const Rect& r) {
        int cx = r.x + 12, cy = r.y + 12;
        uint32_t k = col::BLACK;
        switch (t) {
        case T_PENCIL:
            s.line(cx - 6, cy + 6, cx + 5, cy - 5, k); s.line(cx - 5, cy + 6, cx + 6, cy - 5, col::YELLOW);
            s.put(cx - 7, cy + 7, k);
            break;
        case T_BRUSH:
            s.line(cx + 6, cy - 7, cx - 1, cy + 1, 0x804000, 2);
            s.fill({cx - 5, cy + 1, 5, 5}, k);
            break;
        case T_AIR:
            s.fill({cx - 2, cy - 2, 6, 9}, col::SHADOW); s.rect({cx - 2, cy - 2, 6, 9}, k);
            s.fill({cx - 1, cy - 5, 4, 3}, k);
            for (int i = 0; i < 8; i++) s.put(cx - 8 + (i * 5) % 6, cy - 8 + (i * 3) % 5, k);
            break;
        case T_ERASER:
            s.fill({cx - 6, cy - 3, 12, 7}, 0xFFC0C0); s.rect({cx - 6, cy - 3, 12, 7}, k);
            break;
        case T_FILL:
            s.fill({cx - 5, cy - 3, 9, 9}, col::WHITE); s.rect({cx - 5, cy - 3, 9, 9}, k);
            s.fill({cx + 4, cy - 2, 3, 7}, col::BLUE);
            break;
        case T_PICK:
            s.line(cx - 6, cy + 6, cx + 3, cy - 3, k, 2); s.fill({cx + 2, cy - 6, 5, 5}, k);
            break;
        case T_LINE: s.line(cx - 7, cy + 6, cx + 6, cy - 7, k); break;
        case T_RECT: s.rect({cx - 7, cy - 5, 14, 10}, k); break;
        case T_ELLIPSE: s.ellipse({cx - 7, cy - 5, 14, 10}, k, false); break;
        case T_TEXT: s.text(font_ui_bold, cx - 4, cy - 7, "A", k); break;
        }
    }

    void on_paint(Surface& s) override {
        s.clear(col::FACE);
        int h = client_h();
        for (int t = 0; t < T_COUNT; t++) {
            Rect r = tool_rect(t);
            if (t == tool) { draw_checker(s, r.inset(1)); s.bevel_sunken(r); }
            else s.bevel_raised(r);
            draw_tool_glyph(s, t, r.offset(t == tool ? 1 : 0, t == tool ? 1 : 0));
        }
        // Opciones de la herramienta
        Rect o = options_rect();
        s.bevel_sunken(o);
        if (tool == T_RECT || tool == T_ELLIPSE) {
            for (int i = 0; i < 3; i++) {
                Rect r{o.x + 4, o.y + 4 + i * 21, o.w - 8, 18};
                if (i == fill_mode) s.fill(r, col::SELECT);
                Rect sh{r.x + 6, r.y + 4, r.w - 12, 10};
                if (i >= 1) s.fill(sh, col::SHADOW);
                if (i <= 1) s.rect(sh, i == fill_mode ? col::WHITE : col::BLACK);
            }
        } else if (tool != T_FILL && tool != T_PICK && tool != T_TEXT) {
            for (int i = 0; i < 4; i++) {
                Rect r{o.x + 4, o.y + 4 + i * 16, o.w - 8, 14};
                if (i == size_opt) s.fill(r, col::SELECT);
                int th = brush_size(i);
                s.fill({r.x + 4, r.y + 7 - th / 2, r.w - 8, th}, i == size_opt ? col::WHITE : col::BLACK);
            }
        }
        // Paleta
        int py = h - STATUS_H - PALETTE_H;
        s.hline(0, py, client_w(), col::HILIGHT);
        Rect cur{6, py + 6, 32, 32};
        s.fill(cur, col::FACE);
        draw_checker(s, cur);
        s.bevel_sunken(cur);
        Rect bgr{cur.x + 12, cur.y + 12, 14, 14};
        s.fill(bgr, bg); s.bevel_raised(bgr); s.fill(bgr.inset(2), bg);
        Rect fgr{cur.x + 4, cur.y + 4, 14, 14};
        s.fill(fgr, fg); s.bevel_raised(fgr); s.fill(fgr.inset(2), fg);
        for (int i = 0; i < 28; i++) {
            Rect r = color_rect(i);
            s.bevel_sunken(r);
            s.fill(r.inset(2), palette[i]);
        }
    }

    static int brush_size(int opt) { static const int sz[4] = {1, 3, 5, 8}; return sz[opt]; }

    void on_mouse_down(const MouseEv& e) override {
        for (int t = 0; t < T_COUNT; t++)
            if (tool_rect(t).contains(e.x, e.y)) { tool = t; status->set_text(T(tool_names[t])); invalidate(); return; }
        Rect o = options_rect();
        if (o.contains(e.x, e.y)) {
            if (tool == T_RECT || tool == T_ELLIPSE) fill_mode = clamp((e.y - o.y - 4) / 21, 0, 2);
            else size_opt = clamp((e.y - o.y - 4) / 16, 0, 3);
            invalidate();
            return;
        }
        for (int i = 0; i < 28; i++) {
            if (color_rect(i).contains(e.x, e.y)) {
                if (e.button == MB_RIGHT) bg = palette[i]; else fg = palette[i];
                if (e.clicks == 2) edit_color(e.button == MB_RIGHT);
                invalidate();
                return;
            }
        }
    }

    void edit_color(bool background) {
        uint32_t& c = background ? bg : fg;
        String v = String::format("%06X", c);
        if (input_box(this, T("Modificar colores"), T("Color en hexadecimal (RRGGBB):"), v)) {
            uint32_t n = 0;
            for (const char* p = v.c_str(); *p; p++) {
                char ch = to_upper(*p);
                int d = (ch >= '0' && ch <= '9') ? ch - '0' : (ch >= 'A' && ch <= 'F') ? ch - 'A' + 10 : -1;
                if (d < 0) break;
                n = n * 16 + d;
            }
            c = n & 0xFFFFFF;
            invalidate();
        }
    }

    void save_undo() {
        if (undo.w != img.w || undo.h != img.h) undo.resize(img.w, img.h);
        memcpy(undo.px, img.px, (size_t)img.w * img.h * 4);
        has_undo = true;
    }

    void do_undo() {
        if (!has_undo) return;
        Surface tmp(img.w, img.h);
        memcpy(tmp.px, img.px, (size_t)img.w * img.h * 4);
        int tw = img.w, th = img.h;
        img.resize(undo.w, undo.h);
        memcpy(img.px, undo.px, (size_t)undo.w * undo.h * 4);
        undo.resize(tw, th);
        memcpy(undo.px, tmp.px, (size_t)tw * th * 4);
        modified = true;
        canvas->invalidate();
    }

    void stamp(int x, int y, uint32_t c, int size) {
        if (size <= 1) { img.put(x, y, c); return; }
        img.fill({x - size / 2, y - size / 2, size, size}, c);
    }

    void stroke(int x0, int y0, int x1, int y1, uint32_t c, int size) {
        int dx = abs_val(x1 - x0), sxs = x0 < x1 ? 1 : -1;
        int dy = -abs_val(y1 - y0), sys = y0 < y1 ? 1 : -1;
        int err = dx + dy;
        for (;;) {
            stamp(x0, y0, c, size);
            if (x0 == x1 && y0 == y1) break;
            int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sxs; }
            if (e2 <= dx) { err += dx; y0 += sys; }
        }
    }

    void spray(int x, int y) {
        int r = brush_size(size_opt) * 2 + 4;
        for (int i = 0; i < 12; i++) {
            rng = rng * 1103515245 + 12345;
            int dx = (int)((rng >> 8) % (2 * r + 1)) - r;
            rng = rng * 1103515245 + 12345;
            int dy = (int)((rng >> 8) % (2 * r + 1)) - r;
            if (dx * dx + dy * dy <= r * r) img.put(x + dx, y + dy, draw_color);
        }
    }

    void flood(int x, int y, uint32_t c) {
        uint32_t target = img.get(x, y);
        if (target == c || x < 0 || y < 0 || x >= img.w || y >= img.h) return;
        struct P { int x, y; };
        Vec<P> st;
        st.push({x, y});
        while (!st.empty()) {
            P p = st.back();
            st.pop();
            int lx2 = p.x;
            uint32_t* row = img.px + p.y * img.stride;
            if (row[lx2] != target) continue;
            while (lx2 > 0 && row[lx2 - 1] == target) lx2--;
            int rx = p.x;
            while (rx < img.w - 1 && row[rx + 1] == target) rx++;
            for (int i = lx2; i <= rx; i++) row[i] = c;
            for (int dir = -1; dir <= 1; dir += 2) {
                int ny = p.y + dir;
                if (ny < 0 || ny >= img.h) continue;
                uint32_t* nr = img.px + ny * img.stride;
                bool in = false;
                for (int i = lx2; i <= rx; i++) {
                    if (nr[i] == target) { if (!in) { st.push({i, ny}); in = true; } }
                    else in = false;
                }
            }
        }
    }

    void shape(int x0, int y0, int x1, int y1, bool shift) {
        memcpy(img.px, undo.px, (size_t)img.w * img.h * 4);
        int th = brush_size(size_opt);
        if (tool == T_LINE) { stroke(x0, y0, x1, y1, draw_color, th); return; }
        if (shift) {
            int d = max(abs_val(x1 - x0), abs_val(y1 - y0));
            x1 = x0 + (x1 >= x0 ? d : -d);
            y1 = y0 + (y1 >= y0 ? d : -d);
        }
        Rect r{min(x0, x1), min(y0, y1), abs_val(x1 - x0) + 1, abs_val(y1 - y0) + 1};
        uint32_t outline = draw_color, fillc = fill_mode == 2 ? draw_color : other_color;
        if (tool == T_RECT) {
            if (fill_mode >= 1) img.fill(r, fillc);
            if (fill_mode <= 1) img.rect(r, outline);
        } else {
            if (fill_mode >= 1) img.ellipse(r, fillc, true);
            if (fill_mode <= 1) img.ellipse(r, outline, false);
        }
    }

    void begin(int x, int y, int button, bool shift) {
        if (button != MB_LEFT && button != MB_RIGHT) return;
        draw_color = button == MB_RIGHT ? bg : fg;
        other_color = button == MB_RIGHT ? fg : bg;
        if (tool == T_PICK) {
            uint32_t c = img.get(x, y);
            if (button == MB_RIGHT) bg = c; else fg = c;
            invalidate();
            return;
        }
        save_undo();
        modified = true;
        if (tool == T_FILL) { flood(x, y, draw_color); canvas->invalidate(); return; }
        if (tool == T_TEXT) {
            String t;
            if (input_box(this, T("Texto"), T("Escriba el texto:"), t) && !t.empty()) {
                img.text(font_ui, x, y, t.c_str(), draw_color);
                canvas->invalidate();
            }
            return;
        }
        drawing = true;
        lx = ox = x;
        ly = oy = y;
        if (tool == T_PENCIL) img.put(x, y, draw_color);
        else if (tool == T_BRUSH) stamp(x, y, draw_color, brush_size(size_opt) + 1);
        else if (tool == T_ERASER) stamp(x, y, bg, brush_size(size_opt) * 2 + 2);
        else if (tool == T_AIR) { spray(x, y); wm::set_timer(this, 1, 40); }
        (void)shift;
        canvas->invalidate();
    }

    void drag(int x, int y, bool shift) {
        status->set_text(String::format(T("%d, %d"), x, y).c_str());
        if (!drawing) return;
        switch (tool) {
        case T_PENCIL: stroke(lx, ly, x, y, draw_color, 1); break;
        case T_BRUSH: stroke(lx, ly, x, y, draw_color, brush_size(size_opt) + 1); break;
        case T_ERASER: stroke(lx, ly, x, y, bg, brush_size(size_opt) * 2 + 2); break;
        case T_AIR: spray(x, y); break;
        case T_LINE: case T_RECT: case T_ELLIPSE: shape(ox, oy, x, y, shift); break;
        }
        lx = x;
        ly = y;
        canvas->invalidate();
    }

    void end() {
        if (!drawing) return;
        drawing = false;
        wm::kill_timer(this, 1);
        canvas->invalidate();
    }

    void on_timer(int) override {
        if (drawing && tool == T_AIR) { spray(lx, ly); canvas->invalidate(); }
    }

    void update_title() {
        String n = path.empty() ? String(T("Sin título")) : vfs::basename(path.c_str());
        set_title((n + T(" - Paint")).c_str());
    }

    bool load(const char* p) {
        uint8_t* data;
        uint32_t size;
        if (!vfs::read_file(p, data, size)) {
            msgbox(this, T("Paint"), String::format(T("No se puede abrir %s.\n%s"), p, vfs::error()).c_str(), MB_OK, &icon_error);
            return false;
        }
        String err;
        Surface tmp;
        String np = vfs::normalize(p);
        bool converted = false;
        bool ok;
        const ImagesApi* api = bsl::get<ImagesApi>("images", IMAGES_API);
        if (vfs::extension(np.c_str()) != "BMP" && api) {
            // otros formatos: se abren con images.bsl (el primer fotograma, sobre blanco)
            Image im;
            const char* e = "";
            ok = api->decode(data, size, im, e);
            if (ok) {
                tmp.resize(im.w, im.h);
                for (size_t i = 0; i < (size_t)im.w * im.h; i++) {
                    uint32_t c = im.frames[0].px[i];
                    tmp.px[i] = blend(0xFFFFFF, c & 0xFFFFFF, (int)(c >> 24));
                }
                api->release(im);
                converted = true;
            } else {
                err = e;
            }
        } else {
            ok = bmp_decode(data, size, tmp, err);
        }
        mm::kfree(data);
        if (!ok) { msgbox(this, T("Paint"), err.c_str(), MB_OK, &icon_error); return false; }
        img.resize(tmp.w, tmp.h);
        memcpy(img.px, tmp.px, (size_t)tmp.w * tmp.h * 4);
        has_undo = false;
        path = np;
        if (converted) {
            // Paint guarda en BMP: el mismo nombre con esa extensión
            int dot = path.rfind('.');
            if (dot > 0) path = path.substr(0, (size_t)dot);
            path += ".bmp";
        }
        modified = false;
        update_title();
        canvas->sx = canvas->sy = 0;
        canvas->invalidate();
        return true;
    }

    bool save(bool ask) {
        String p = path;
        if (ask || p.empty()) {
            if (p.empty()) p = vfs::join(vfs::pictures_dir().c_str(), T("Sin título.bmp"));
            if (!file_dialog(this, true, T("Guardar como"), "BMP", p)) return false;
        }
        Vec<uint8_t> data = bmp_encode(img);
        wm::set_busy(true);
        bool ok = vfs::write_file(p.c_str(), data.data(), (uint32_t)data.size());
        wm::set_busy(false);
        if (!ok) { msgbox(this, T("Paint"), String::format(T("No se puede guardar %s.\n%s"), p.c_str(), vfs::error()).c_str(), MB_OK, &icon_error); return false; }
        path = vfs::normalize(p.c_str());
        modified = false;
        update_title();
        return true;
    }

    bool confirm_discard() {
        if (!modified) return true;
        String n = path.empty() ? String(T("Sin título")) : vfs::basename(path.c_str());
        int r = msgbox(this, T("Paint"), String::format(T("¿Desea guardar los cambios en %s?"), n.c_str()).c_str(), MB_YESNOCANCEL, &icon_warning);
        if (r == ID_CANCEL) return false;
        if (r == ID_YES) return save(false);
        return true;
    }

    bool on_close() override { return confirm_discard(); }

    int parse_int(const String& s) {
        int n = 0;
        for (const char* p = s.c_str(); *p >= '0' && *p <= '9'; p++) n = n * 10 + (*p - '0');
        return n;
    }

    void on_command(int id, int) override {
        switch (id) {
        case M_NEW:
            if (!confirm_discard()) return;
            img.resize(480, 320);
            img.clear(0xFFFFFF);
            path.clear();
            modified = false;
            has_undo = false;
            update_title();
            canvas->invalidate();
            return;
        case M_OPEN: {
            if (!confirm_discard()) return;
            String p = path.empty() ? vfs::pictures_dir() : vfs::parent(path.c_str());
            if (file_dialog(this, false, T("Abrir"), nullptr, p)) load(p.c_str());
            return;
        }
        case M_SAVE: save(false); return;
        case M_SAVEAS: save(true); return;
        case M_EXIT: close(); return;
        case M_UNDO: do_undo(); return;
        case M_CLEAR: save_undo(); img.clear(bg); modified = true; canvas->invalidate(); return;
        case M_INVERT:
            save_undo();
            for (int i = 0; i < img.w * img.h; i++) img.px[i] ^= 0xFFFFFF;
            modified = true; canvas->invalidate(); return;
        case M_FLIPH:
            save_undo();
            for (int y = 0; y < img.h; y++)
                for (int x = 0; x < img.w / 2; x++) {
                    uint32_t* r = img.px + y * img.stride;
                    uint32_t t = r[x]; r[x] = r[img.w - 1 - x]; r[img.w - 1 - x] = t;
                }
            modified = true; canvas->invalidate(); return;
        case M_FLIPV:
            save_undo();
            for (int y = 0; y < img.h / 2; y++)
                for (int x = 0; x < img.w; x++) {
                    uint32_t t = img.px[y * img.stride + x];
                    img.px[y * img.stride + x] = img.px[(img.h - 1 - y) * img.stride + x];
                    img.px[(img.h - 1 - y) * img.stride + x] = t;
                }
            modified = true; canvas->invalidate(); return;
        case M_ATTR: {
            String v = String::format(T("%dx%d"), img.w, img.h);
            if (!input_box(this, T("Atributos"), T("Tamaño de la imagen en píxeles (ancho x alto):"), v)) return;
            int x = v.find('x');
            if (x < 0) x = v.find('X');
            int nw = parse_int(v), nh = x >= 0 ? parse_int(v.substr((size_t)x + 1)) : 0;
            if (nw < 1 || nh < 1 || nw > 4096 || nh > 4096) { msgbox(this, T("Paint"), T("Tamaño no válido."), MB_OK, &icon_warning); return; }
            save_undo();
            Surface tmp(nw, nh);
            tmp.clear(bg);
            tmp.blit(img, 0, 0);
            img.resize(nw, nh);
            memcpy(img.px, tmp.px, (size_t)nw * nh * 4);
            modified = true;
            canvas->invalidate();
            return;
        }
        case M_ABOUT:
            msgbox(this, T("Acerca de Paint"), T("Paint de BrysOS\nVersión 1.0\n\nGuarda imágenes en formato BMP de 24 bits."), MB_OK, &icon_paint);
            return;
        }
    }

    bool on_key(const InputEvent& e) override {
        if (!e.pressed || !(e.mods & MOD_CTRL)) return false;
        bool en = i18n::language() == Lang::EN;
        uint16_t k = e.key;
        if (k == 'n' && (e.mods & MOD_SHIFT)) { on_command(M_CLEAR, 0); return true; }
        if (k == 'z') { on_command(M_UNDO, 0); return true; }
        if (k == (en ? 's' : 'g')) { on_command(M_SAVE, 0); return true; }
        if (k == (en ? 'o' : 'a')) { on_command(M_OPEN, 0); return true; }
        if (k == (en ? 'n' : 'u')) { on_command(M_NEW, 0); return true; }
        if (k == 'i') { on_command(M_INVERT, 0); return true; }
        if (k == 'e') { on_command(M_ATTR, 0); return true; }
        return false;
    }
};

void Canvas::layout() {
    Surface& img = pw->img;
    // cada barra sólo aparece si la imagen no cabe
    bool need_h = img.w + 8 > r.w, need_v = img.h + 8 > r.h;
    if (need_h && !need_v) need_v = img.h + 8 > r.h - 16;
    if (need_v && !need_h) need_h = img.w + 8 > r.w - 16;
    int vw = r.w - (need_v ? 16 : 0), vh = r.h - (need_h ? 16 : 0);
    vs.r = need_v ? Rect{r.w - 16, 0, 16, vh} : Rect{};
    hs.r = need_h ? Rect{0, r.h - 16, vw, 16} : Rect{};
    vs.total = img.h + 8; vs.page = vh; vs.line = 20;
    hs.total = img.w + 8; hs.page = vw; hs.line = 20;
    vs.set(sy); hs.set(sx);
    sy = vs.pos; sx = hs.pos;
}

void Canvas::paint(Surface& s) {
    layout();
    Surface& img = pw->img;
    s.fill({0, 0, r.w, r.h}, col::SHADOW);
    int vw = r.w - (vs.r.empty() ? 0 : 16), vh = r.h - (hs.r.empty() ? 0 : 16);
    {
        PaintScope ps(s, {0, 0, vw, vh});
        s.blit(img, 4 - sx, 4 - sy);
        // tirador de tamaño
        s.fill({4 - sx + img.w, 4 - sy + img.h, 3, 3}, col::SELECT);
    }
    if (!vs.r.empty()) vs.paint(s);
    if (!hs.r.empty()) hs.paint(s);
    if (!vs.r.empty() && !hs.r.empty()) s.fill({r.w - 16, r.h - 16, 16, 16}, col::FACE);
}

Cursor Canvas::cursor(int x, int y) {
    if (vs.r.contains(x, y) || hs.r.contains(x, y)) return CURSOR_ARROW;
    return CURSOR_CROSS;
}

void Canvas::mouse_down(const MouseEv& e) {
    layout();
    bool ch;
    if (!vs.r.empty() && vs.mouse_down(e.x, e.y, ch)) { sb = 1; sy = vs.pos; invalidate(); return; }
    if (!hs.r.empty() && hs.mouse_down(e.x, e.y, ch)) { sb = 2; sx = hs.pos; invalidate(); return; }
    pw->begin(e.x - 4 + sx, e.y - 4 + sy, e.button, e.mods & MOD_SHIFT);
}

void Canvas::mouse_move(const MouseEv& e) {
    bool ch;
    if (sb == 1) { vs.mouse_move(e.x, e.y, ch); sy = vs.pos; if (ch) invalidate(); return; }
    if (sb == 2) { hs.mouse_move(e.x, e.y, ch); sx = hs.pos; if (ch) invalidate(); return; }
    pw->drag(e.x - 4 + sx, e.y - 4 + sy, e.mods & MOD_SHIFT);
}

void Canvas::mouse_up(const MouseEv&) {
    if (sb) { vs.mouse_up(); hs.mouse_up(); sb = 0; invalidate(); return; }
    pw->end();
}
}

namespace apps {
void paint(const char* path) {
    wm::add(new PaintWin(path));
}
}

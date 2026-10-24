// Visor de imágenes: abre BMP, PNG, JPEG y GIF (animados) con images.bsl
#include "apps/common.h"
#include "bsl/loader.h"
#include "bsl/images.h"

namespace {
enum { ID_PREV = 10, ID_NEXT, ID_FIT, ID_REAL, ID_ROT_L, ID_ROT_R, ID_OPEN, ID_WALL };
constexpr int BAR_H = 30, STATUS_H = 20;

const ImagesApi* images() { return bsl::get<ImagesApi>("images", IMAGES_API); }

class Viewer : public Window {
public:
    const ImagesApi* api;
    Image img;
    bool loaded = false;
    String path;
    Vec<String> siblings;   // imágenes de la misma carpeta
    int index = -1;
    int frame = 0;
    bool fit = true;
    int rotation = 0;       // cuartos de vuelta en el sentido de las agujas del reloj
    int pan_x = 0, pan_y = 0;
    bool dragging = false;
    int drag_x = 0, drag_y = 0, drag_px = 0, drag_py = 0;
    // imagen ya escalada y girada para la ventana actual
    Surface view;
    int view_key[5] = {-1, -1, -1, -1, -1};
    Label* status;
    Label* status2;
    Button* fit_btn;
    Button* real_btn;

    Viewer() : Window(T("Visor de imágenes"), 860, 560, WF_NORMAL), api(images()) {
        icon = &icon_file_image;
        min_w = 360;
        min_h = 240;
        int x = 4;
        auto btn = [&](const char* text, int id, int w, const IconPair* ic = nullptr) {
            auto* b = add(new Button({x, 3, w, 24}, text, id));
            b->flat = true;
            if (ic) b->icon = &ic->small;
            x += w + 2;
            return b;
        };
        btn(T("Anterior"), ID_PREV, 78, &icon_arrow_left);
        btn(T("Siguiente"), ID_NEXT, 82, &icon_arrow_right);
        x += 8;
        fit_btn = btn(T("Ajustar"), ID_FIT, 64);
        real_btn = btn(T("Tamaño real"), ID_REAL, 86);
        fit_btn->toggle = real_btn->toggle = true;
        x += 8;
        btn(T("Girar a la izquierda"), ID_ROT_L, 126);
        btn(T("Girar a la derecha"), ID_ROT_R, 120);
        x += 8;
        btn(T("Abrir..."), ID_OPEN, 64);
        btn(T("Usar como fondo"), ID_WALL, 112);
        status = add(new Label({0, 0, 10, 10}, ""));
        status->sunken = true;
        status2 = add(new Label({0, 0, 10, 10}, ""));
        status2->sunken = true;
        update_buttons();
        on_resize();
    }

    ~Viewer() override {
        if (loaded) api->release(img);
    }

    Rect area() const { return {0, BAR_H, client_w(), client_h() - BAR_H - STATUS_H}; }

    void on_resize() override {
        int w = client_w(), h = client_h();
        int s1 = w * 3 / 5;
        status->r = {0, h - STATUS_H, s1 - 2, STATUS_H};
        status2->r = {s1, h - STATUS_H, w - s1, STATUS_H};
        invalidate();
    }

    void update_buttons() {
        fit_btn->down = fit;
        real_btn->down = !fit;
        fit_btn->invalidate();
        real_btn->invalidate();
    }

    // --- abrir
    void list_folder() {
        siblings.clear();
        index = -1;
        String dir = vfs::parent(path.c_str());
        Vec<VfsEntry> ents;
        if (!vfs::list(dir.c_str(), ents)) return;
        ents.sort([](const VfsEntry& a, const VfsEntry& b) { return stricmp(a.name.c_str(), b.name.c_str()); });
        for (auto& e : ents)
            if (!e.dir && api->can_open(vfs::extension(e.name.c_str()).c_str())) {
                String full = vfs::join(dir.c_str(), e.name.c_str());
                if (!stricmp(full.c_str(), path.c_str())) index = (int)siblings.size();
                siblings.push(full);
            }
    }

    bool open(const char* p, bool relist = true) {
        uint8_t* data;
        uint32_t size;
        String np = vfs::normalize(p);
        if (!vfs::read_file(np.c_str(), data, size)) {
            msgbox(this, T("Visor de imágenes"), String::format(T("No se puede abrir %s.\n%s"), np.c_str(), vfs::error()).c_str(), MB_OK, &icon_error);
            return false;
        }
        Image ni;
        const char* err = "";
        wm::set_busy(true);
        bool ok = api->decode(data, size, ni, err);
        wm::set_busy(false);
        mm::kfree(data);
        if (!ok) {
            msgbox(this, T("Visor de imágenes"), String::format(T("No se puede mostrar %s.\n\n%s"), vfs::basename(np.c_str()).c_str(), err).c_str(),
                   MB_OK, &icon_error);
            return false;
        }
        if (loaded) api->release(img);
        img = ni;
        loaded = true;
        path = np;
        frame = 0;
        rotation = 0;
        pan_x = pan_y = 0;
        view_key[0] = -1;
        if (relist) list_folder();
        set_title((vfs::basename(path.c_str()) + T(" - Visor de imágenes")).c_str());
        wm::kill_timer(this, 1);
        if (img.frame_count > 1) wm::set_timer(this, 1, img.frames[0].delay_ms);
        update_status();
        invalidate();
        return true;
    }

    void go(int delta) {
        if (siblings.empty()) return;
        int n = (int)siblings.size();
        for (int tries = 0; tries < n; tries++) {
            index = ((index < 0 ? 0 : index) + delta + n) % n;
            if (open(siblings[(size_t)index].c_str(), false)) return;
        }
    }

    void update_status() {
        if (!loaded) {
            status->set_text(T("Abra una imagen con el botón Abrir."));
            status2->set_text("");
            return;
        }
        String s = String::format(T("%s   %d x %d   %s"), vfs::basename(path.c_str()).c_str(), img.w, img.h, img.format);
        if (img.frame_count > 1) s += String::format(T("   %d fotogramas"), img.frame_count);
        status->set_text(s.c_str());
        String s2;
        if (index >= 0) s2 = String::format(T("%d de %d"), index + 1, (int)siblings.size());
        s2 += String::format(T("   Zoom: %d%%"), current_zoom());
        status2->set_text(s2.c_str());
    }

    // --- dibujo
    int iw() const { return (rotation & 1) ? img.h : img.w; }
    int ih() const { return (rotation & 1) ? img.w : img.h; }

    int current_zoom() const {
        if (!loaded) return 100;
        if (!fit) return 100;
        Rect a = area();
        int z = 100;
        if (iw() > a.w || ih() > a.h) z = min(a.w * 100 / iw(), a.h * 100 / ih());
        return max(1, z);
    }

    // Tamaño en pantalla
    void target_size(int& tw, int& th) const {
        Rect a = area();
        tw = iw();
        th = ih();
        if (fit && (tw > a.w || th > a.h)) {
            if ((int64_t)tw * a.h > (int64_t)th * a.w) { th = max(1, (int)((int64_t)th * a.w / tw)); tw = a.w; }
            else { tw = max(1, (int)((int64_t)tw * a.h / th)); th = a.h; }
        }
    }

    // Píxel (x, y) de la imagen ya girada
    uint32_t src_px(const uint32_t* px, int x, int y) const {
        int W = img.w, H = img.h;
        switch (rotation & 3) {
        case 1: return px[(size_t)(H - 1 - x) * W + y];
        case 2: return px[(size_t)(H - 1 - y) * W + (W - 1 - x)];
        case 3: return px[(size_t)x * W + (W - 1 - y)];
        default: return px[(size_t)y * W + x];
        }
    }

    // Reducción por promedio de áreas (sin perder detalle de golpe) y giro
    void build_view() {
        int tw, th;
        target_size(tw, th);
        int key[5] = {tw, th, rotation, frame, (int)(uintptr_t)img.frames};
        bool same = true;
        for (int i = 0; i < 5; i++) same = same && key[i] == view_key[i];
        if (same) return;
        for (int i = 0; i < 5; i++) view_key[i] = key[i];
        view.resize(tw, th);
        const uint32_t* px = img.frames[frame].px;
        int sw = iw(), sh = ih();
        for (int y = 0; y < th; y++) {
            int y0 = (int)((int64_t)y * sh / th), y1 = max(y0 + 1, (int)((int64_t)(y + 1) * sh / th));
            for (int x = 0; x < tw; x++) {
                int x0 = (int)((int64_t)x * sw / tw), x1 = max(x0 + 1, (int)((int64_t)(x + 1) * sw / tw));
                // como mucho 8x8 muestras por píxel (suficiente y rápido en fotos enormes)
                int sx = max(1, (x1 - x0) / 8), sy = max(1, (y1 - y0) / 8);
                uint32_t a = 0, r = 0, g = 0, b = 0, n = 0;
                for (int yy = y0; yy < y1; yy += sy)
                    for (int xx = x0; xx < x1; xx += sx) {
                        uint32_t p = src_px(px, xx, yy);
                        uint32_t al = p >> 24;
                        a += al;
                        r += ((p >> 16) & 255) * al;
                        g += ((p >> 8) & 255) * al;
                        b += (p & 255) * al;
                        n++;
                    }
                uint32_t out = 0;
                if (a) out = ((a / n) << 24) | ((r / a) << 16) | ((g / a) << 8) | (b / a);
                view.px[(size_t)y * view.stride + x] = out;
            }
        }
    }

    void on_paint(Surface& s) override {
        int w = client_w(), h = client_h();
        s.fill({0, 0, w, BAR_H}, col::FACE);
        s.bevel_thin_raised({0, 0, w, BAR_H});
        Rect a = area();
        s.fill(a, 0x808080);
        if (!loaded) return;
        build_view();
        int dx = a.x + (a.w - view.w) / 2 + (fit ? 0 : pan_x);
        int dy = a.y + (a.h - view.h) / 2 + (fit ? 0 : pan_y);
        // transparencia sobre un damero, como los editores de imágenes
        for (int y = 0; y < view.h; y++) {
            int py = dy + y;
            if (py < a.y || py >= a.bottom()) continue;
            for (int x = 0; x < view.w; x++) {
                int px = dx + x;
                if (px < a.x || px >= a.right()) continue;
                uint32_t p = view.px[(size_t)y * view.stride + x];
                uint32_t al = p >> 24;
                if (al < 255) {
                    uint32_t bg = (((x >> 3) + (y >> 3)) & 1) ? 0xFFFFFF : 0xCCCCCC;
                    p = blend(bg, p & 0xFFFFFF, (int)al);
                }
                s.put(px, py, p & 0xFFFFFF);
            }
        }
    }

    // --- eventos
    void on_command(int id, int) override {
        switch (id) {
        case ID_PREV: go(-1); break;
        case ID_NEXT: go(1); break;
        case ID_FIT: fit = true; update_buttons(); update_status(); invalidate(); break;
        case ID_REAL: fit = false; pan_x = pan_y = 0; update_buttons(); update_status(); invalidate(); break;
        case ID_ROT_L: if (loaded) { rotation = (rotation + 3) & 3; update_status(); invalidate(); } break;
        case ID_ROT_R: if (loaded) { rotation = (rotation + 1) & 3; update_status(); invalidate(); } break;
        case ID_WALL: if (loaded) apps::wallpaper_dialog(path.c_str()); break;
        case ID_OPEN: {
            String p = path.empty() ? vfs::pictures_dir() : vfs::parent(path.c_str());
            if (file_dialog(this, false, T("Abrir"), nullptr, p)) open(p.c_str());
            break;
        }
        }
    }

    void on_timer(int) override {
        if (!loaded || img.frame_count < 2) return;
        frame = (frame + 1) % img.frame_count;
        wm::set_timer(this, 1, max<uint32_t>(20, img.frames[frame].delay_ms));
        invalidate();
    }

    bool on_key(const InputEvent& e) override {
        if (!e.pressed) return false;
        switch (e.key) {
        case KEY_LEFT: case KEY_PGUP: case KEY_BACKSPACE: go(-1); return true;
        case KEY_RIGHT: case KEY_PGDN: case ' ': go(1); return true;
        case KEY_HOME: if (!siblings.empty()) { index = (int)siblings.size() - 1; go(1); } return true;
        case KEY_END: if (!siblings.empty()) { index = 0; go(-1); } return true;
        }
        if (e.ch == 'r' || e.ch == 'R') { on_command(ID_ROT_R, 0); return true; }
        return false;
    }

    void on_mouse_wheel(const MouseEv& e) override { go(e.wheel > 0 ? -1 : 1); }

    void on_mouse_down(const MouseEv& e) override {
        if (fit || !loaded || !area().contains(e.x, e.y)) return;
        dragging = true;
        drag_x = e.x;
        drag_y = e.y;
        drag_px = pan_x;
        drag_py = pan_y;
    }
    void on_mouse_move(const MouseEv& e) override {
        if (!dragging) return;
        // se puede arrastrar hasta que el borde de la imagen llegue al de la ventana
        Rect a = area();
        int lx = max(0, (view.w - a.w) / 2), ly = max(0, (view.h - a.h) / 2);
        pan_x = clamp(drag_px + e.x - drag_x, -lx, lx);
        pan_y = clamp(drag_py + e.y - drag_y, -ly, ly);
        invalidate();
    }
    void on_mouse_up(const MouseEv&) override { dragging = false; }
    Cursor client_cursor(int x, int y) override { return CURSOR_ARROW; }

    void retranslate() override {
        Window::retranslate();
        if (loaded) set_title((vfs::basename(path.c_str()) + T(" - Visor de imágenes")).c_str());
        update_status();
    }
};
}

namespace apps {
void viewer(const char* path) {
    if (!images()) {
        msgbox(nullptr, T("Visor de imágenes"),
               T("Falta la biblioteca del sistema images.bsl, que es la que lee las imágenes.\n"
                 "Vuelva a instalarla con un paquete de actualización (.bsi)."),
               MB_OK, &icon_error);
        return;
    }
    auto* v = new Viewer();
    wm::add(v);
    if (path && !v->open(path)) {
        wm::destroy(v);
        return;
    }
    if (!path) v->update_status();
}

bool image_file(const char* ext) {
    const ImagesApi* api = images();
    return api && api->can_open(ext);
}
}

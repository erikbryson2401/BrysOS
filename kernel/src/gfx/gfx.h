#pragma once
#include <stdint.h>
#include "gfx/font.h"

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
    constexpr Rect() = default;
    constexpr Rect(int x_, int y_, int w_, int h_) : x(x_), y(y_), w(w_), h(h_) {}
    int right() const { return x + w; }
    int bottom() const { return y + h; }
    bool empty() const { return w <= 0 || h <= 0; }
    bool contains(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
    Rect intersect(const Rect& o) const;
    Rect unite(const Rect& o) const;
    bool intersects(const Rect& o) const { return !intersect(o).empty(); }
    Rect offset(int dx, int dy) const { return {x + dx, y + dy, w, h}; }
    Rect inset(int d) const { return {x + d, y + d, w - 2 * d, h - 2 * d}; }
};

// Paleta clásica de Windows 98
namespace col {
constexpr uint32_t BLACK = 0x000000, WHITE = 0xFFFFFF;
constexpr uint32_t FACE = 0xC0C0C0;       // gris de botones
constexpr uint32_t SHADOW = 0x808080;     // sombra
constexpr uint32_t DKSHADOW = 0x000000;
constexpr uint32_t LIGHT = 0xDFDFDF;
constexpr uint32_t HILIGHT = 0xFFFFFF;
constexpr uint32_t DESKTOP = 0x008080;    // verde azulado
constexpr uint32_t TITLE_A1 = 0x000080, TITLE_A2 = 0x1084D0; // título activo (degradado)
constexpr uint32_t TITLE_I1 = 0x808080, TITLE_I2 = 0xB5B5B5; // título inactivo
constexpr uint32_t SELECT = 0x000080;
constexpr uint32_t TOOLTIP = 0xFFFFE1;
constexpr uint32_t RED = 0xFF0000, GREEN = 0x008000, BLUE = 0x0000FF, YELLOW = 0xFFFF00;
constexpr uint32_t TRANSPARENT = 0xFF00FF; // color clave para iconos
}

class Surface {
public:
    uint32_t* px = nullptr;
    int w = 0, h = 0;
    int stride = 0; // en píxeles
    Rect clip;          // en coordenadas absolutas de la superficie
    int ox = 0, oy = 0; // origen: se suma a todas las coordenadas de dibujo
    bool owns = false;

    constexpr Surface() = default;
    Surface(int width, int height);
    ~Surface();
    Surface(const Surface&) = delete;
    Surface& operator=(const Surface&) = delete;
    void resize(int width, int height);
    void wrap(uint32_t* buf, int width, int height, int stride_px);

    void reset_clip() { clip = {0, 0, w, h}; ox = oy = 0; }
    void set_clip(const Rect& r) { clip = r.intersect({0, 0, w, h}); }

    uint32_t get(int x, int y) const {
        x += ox; y += oy;
        if (x < 0 || y < 0 || x >= w || y >= h) return 0;
        return px[y * stride + x];
    }
    void put(int x, int y, uint32_t c) {
        x += ox; y += oy;
        if (clip.contains(x, y)) px[y * stride + x] = c;
    }
    void fill(const Rect& r, uint32_t c);
    void clear(uint32_t c) { fill({0, 0, w, h}, c); }
    void hline(int x, int y, int len, uint32_t c) { fill({x, y, len, 1}, c); }
    void vline(int x, int y, int len, uint32_t c) { fill({x, y, 1, len}, c); }
    void rect(const Rect& r, uint32_t c);
    void dotted_rect(const Rect& r, uint32_t c);
    void line(int x0, int y0, int x1, int y1, uint32_t c, int thickness = 1);
    void ellipse(const Rect& r, uint32_t c, bool filled);
    void gradient_h(const Rect& r, uint32_t c1, uint32_t c2);
    void invert(const Rect& r);

    // Copia de otra superficie; key != 0xFFFFFFFF actúa como transparente
    void blit(const Surface& src, int dx, int dy, const Rect& srect, uint32_t key = 0xFFFFFFFF);
    void blit(const Surface& src, int dx, int dy) { blit(src, dx, dy, {0, 0, src.w, src.h}); }
    void blit_scaled(const Surface& src, const Rect& dst);

    // Texto (Latin-1). Devuelve la anchura dibujada.
    int text(const Font& f, int x, int y, const char* s, uint32_t c, int len = -1);
    void text_centered(const Font& f, const Rect& r, const char* s, uint32_t c);
    // Texto con '&' marcando la letra subrayada (atajo de teclado)
    int text_mnemonic(const Font& f, int x, int y, const char* s, uint32_t c);

    // Bordes 3D al estilo Win98
    void bevel_raised(const Rect& r);      // botón normal
    void bevel_sunken(const Rect& r);      // campo de texto
    void bevel_pressed(const Rect& r);     // botón presionado
    void bevel_thin_raised(const Rect& r); // barra de tareas, paneles
    void bevel_thin_sunken(const Rect& r); // barra de estado
    void etched(const Rect& r);            // grupos
};

// Cambia temporalmente el origen y el recorte de una superficie.
// 'area' está en coordenadas relativas al origen actual.
struct PaintScope {
    Surface& s;
    Rect old_clip;
    int old_ox, old_oy;
    PaintScope(Surface& surf, const Rect& area) : s(surf), old_clip(surf.clip), old_ox(surf.ox), old_oy(surf.oy) {
        Rect abs = area.offset(s.ox, s.oy);
        s.clip = s.clip.intersect(abs);
        s.ox = abs.x;
        s.oy = abs.y;
    }
    ~PaintScope() { s.clip = old_clip; s.ox = old_ox; s.oy = old_oy; }
};

namespace gfx {
// Inicializa con el framebuffer de Limine
void init();
bool ready();
int width();
int height();
Surface& backbuffer();
// Copia una región del búfer trasero a la pantalla real
void present(const Rect& r);
void present_all();
}

uint32_t blend(uint32_t a, uint32_t b, int t256);

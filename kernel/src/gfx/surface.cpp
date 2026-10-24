#include "gfx/gfx.h"
#include "mm/memory.h"
#include "lib/string.h"

Rect Rect::intersect(const Rect& o) const {
    int x0 = max(x, o.x), y0 = max(y, o.y);
    int x1 = min(right(), o.right()), y1 = min(bottom(), o.bottom());
    if (x1 <= x0 || y1 <= y0) return {x0, y0, 0, 0};
    return {x0, y0, x1 - x0, y1 - y0};
}

Rect Rect::unite(const Rect& o) const {
    if (empty()) return o;
    if (o.empty()) return *this;
    int x0 = min(x, o.x), y0 = min(y, o.y);
    int x1 = max(right(), o.right()), y1 = max(bottom(), o.bottom());
    return {x0, y0, x1 - x0, y1 - y0};
}

uint32_t blend(uint32_t a, uint32_t b, int t) {
    uint32_t r = (((a >> 16) & 255) * (256 - t) + ((b >> 16) & 255) * t) >> 8;
    uint32_t g = (((a >> 8) & 255) * (256 - t) + ((b >> 8) & 255) * t) >> 8;
    uint32_t bl = ((a & 255) * (256 - t) + (b & 255) * t) >> 8;
    return (r << 16) | (g << 8) | bl;
}

Surface::Surface(int width, int height) { resize(width, height); }

Surface::~Surface() {
    if (owns) mm::kfree(px);
}

void Surface::resize(int width, int height) {
    if (owns) mm::kfree(px);
    w = width > 0 ? width : 1;
    h = height > 0 ? height : 1;
    stride = w;
    px = (uint32_t*)mm::kmalloc((size_t)w * h * 4);
    owns = true;
    reset_clip();
}

void Surface::wrap(uint32_t* buf, int width, int height, int stride_px) {
    if (owns) mm::kfree(px);
    px = buf;
    w = width;
    h = height;
    stride = stride_px;
    owns = false;
    reset_clip();
}

void Surface::fill(const Rect& r, uint32_t c) {
    Rect a = r.offset(ox, oy).intersect(clip);
    if (a.empty()) return;
    for (int y = a.y; y < a.bottom(); y++) {
        uint32_t* p = px + y * stride + a.x;
        int n = a.w;
        asm volatile("rep stosl" : "+D"(p), "+c"(n) : "a"(c) : "memory");
    }
}

void Surface::rect(const Rect& r, uint32_t c) {
    if (r.empty()) return;
    hline(r.x, r.y, r.w, c);
    hline(r.x, r.bottom() - 1, r.w, c);
    vline(r.x, r.y, r.h, c);
    vline(r.right() - 1, r.y, r.h, c);
}

void Surface::dotted_rect(const Rect& r, uint32_t c) {
    for (int x = r.x; x < r.right(); x += 2) { put(x, r.y, c); put(x, r.bottom() - 1, c); }
    for (int y = r.y; y < r.bottom(); y += 2) { put(r.x, y, c); put(r.right() - 1, y, c); }
}

void Surface::invert(const Rect& r) {
    Rect a = r.offset(ox, oy).intersect(clip);
    for (int y = a.y; y < a.bottom(); y++)
        for (int x = a.x; x < a.right(); x++) px[y * stride + x] ^= 0xFFFFFF;
}

void Surface::line(int x0, int y0, int x1, int y1, uint32_t c, int th) {
    int dx = abs_val(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs_val(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        if (th <= 1) put(x0, y0, c);
        else fill({x0 - th / 2, y0 - th / 2, th, th}, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void Surface::ellipse(const Rect& r, uint32_t c, bool filled) {
    if (r.w <= 0 || r.h <= 0) return;
    // Algoritmo de punto medio sobre enteros (sin coma flotante)
    int64_t a = r.w / 2, b = r.h / 2;
    int cx = r.x + (int)a, cy = r.y + (int)b;
    int ex = (r.w & 1) ? 0 : 1, ey = (r.h & 1) ? 0 : 1; // tamaños pares
    if (a == 0 || b == 0) { fill(r, c); return; }
    auto plot4 = [&](int x, int y) {
        if (filled) {
            hline(cx - x, cy - y, 2 * x + 1 - ex, c);
            hline(cx - x, cy + y - ey, 2 * x + 1 - ex, c);
        } else {
            put(cx + x - ex, cy + y - ey, c); put(cx - x, cy + y - ey, c);
            put(cx + x - ex, cy - y, c); put(cx - x, cy - y, c);
        }
    };
    int64_t a2 = a * a, b2 = b * b;
    int64_t x = 0, y = b;
    int64_t px_ = 0, py_ = 2 * a2 * y;
    int64_t p = b2 - a2 * b + a2 / 4;
    while (px_ < py_) {
        plot4((int)x, (int)y);
        x++; px_ += 2 * b2;
        if (p < 0) p += b2 + px_;
        else { y--; py_ -= 2 * a2; p += b2 + px_ - py_; }
    }
    p = b2 * (2 * x + 1) * (2 * x + 1) / 4 + a2 * (y - 1) * (y - 1) - a2 * b2;
    while (y >= 0) {
        plot4((int)x, (int)y);
        y--; py_ -= 2 * a2;
        if (p > 0) p += a2 - py_;
        else { x++; px_ += 2 * b2; p += a2 - py_ + px_; }
    }
}

void Surface::gradient_h(const Rect& r, uint32_t c1, uint32_t c2) {
    if (r.w <= 0) return;
    for (int x = 0; x < r.w; x++) {
        int t = r.w > 1 ? x * 256 / (r.w - 1) : 0;
        if (t > 256) t = 256;
        vline(r.x + x, r.y, r.h, blend(c1, c2, t));
    }
}

void Surface::blit(const Surface& src, int dx, int dy, const Rect& srect, uint32_t key) {
    dx += ox;
    dy += oy;
    Rect s = srect.intersect({0, 0, src.w, src.h});
    Rect d = Rect{dx + (s.x - srect.x), dy + (s.y - srect.y), s.w, s.h}.intersect(clip);
    if (d.empty()) return;
    int sx = s.x + (d.x - (dx + (s.x - srect.x)));
    int sy = s.y + (d.y - (dy + (s.y - srect.y)));
    for (int y = 0; y < d.h; y++) {
        const uint32_t* sp = src.px + (sy + y) * src.stride + sx;
        uint32_t* dp = px + (d.y + y) * stride + d.x;
        if (key == 0xFFFFFFFF) {
            memcpy(dp, sp, d.w * 4);
        } else {
            for (int x = 0; x < d.w; x++)
                if (sp[x] != key) dp[x] = sp[x];
        }
    }
}

void Surface::blit_scaled(const Surface& src, const Rect& dst) {
    if (dst.empty()) return;
    for (int y = 0; y < dst.h; y++) {
        int sy = y * src.h / dst.h;
        for (int x = 0; x < dst.w; x++) {
            uint32_t c = src.px[sy * src.stride + x * src.w / dst.w];
            if (c != col::TRANSPARENT) put(dst.x + x, dst.y + y, c);
        }
    }
}

int Surface::text(const Font& f, int x, int y, const char* s, uint32_t c, int len) {
    int x0 = x;
    for (int i = 0; s[i] && (len < 0 || i < len); i++) {
        uint8_t ch = (uint8_t)s[i];
        int cw = f.char_width(ch);
        if (cw == 0) continue;
        if (x + ox + cw > clip.x && x + ox < clip.right() && y + oy + f.height > clip.y && y + oy < clip.bottom()) {
            const uint32_t* g = f.glyph(ch);
            for (int row = 0; row < f.height; row++) {
                uint32_t bits = g[row];
                if (!bits) continue;
                for (int col_ = 0; col_ < cw; col_++)
                    if (bits & (0x80000000u >> col_)) put(x + col_, y + row, c);
            }
        }
        x += cw;
    }
    return x - x0;
}

void Surface::text_centered(const Font& f, const Rect& r, const char* s, uint32_t c) {
    int tw = f.text_width(s);
    text(f, r.x + (r.w - tw) / 2, r.y + (r.h - f.height) / 2, s, c);
}

int Surface::text_mnemonic(const Font& f, int x, int y, const char* s, uint32_t c) {
    int x0 = x;
    for (int i = 0; s[i]; i++) {
        if (s[i] == '&' && s[i + 1] && s[i + 1] != '&') {
            int cw = f.char_width((uint8_t)s[i + 1]);
            hline(x, y + f.ascent + 1, cw, c);
            continue;
        }
        if (s[i] == '&' && s[i + 1] == '&') i++;
        x += text(f, x, y, s + i, c, 1);
    }
    return x - x0;
}

void Surface::bevel_raised(const Rect& r) {
    hline(r.x, r.y, r.w - 1, col::HILIGHT);
    vline(r.x, r.y, r.h - 1, col::HILIGHT);
    hline(r.x + 1, r.y + 1, r.w - 3, col::LIGHT);
    vline(r.x + 1, r.y + 1, r.h - 3, col::LIGHT);
    hline(r.x, r.bottom() - 1, r.w, col::DKSHADOW);
    vline(r.right() - 1, r.y, r.h, col::DKSHADOW);
    hline(r.x + 1, r.bottom() - 2, r.w - 2, col::SHADOW);
    vline(r.right() - 2, r.y + 1, r.h - 2, col::SHADOW);
}

void Surface::bevel_sunken(const Rect& r) {
    hline(r.x, r.y, r.w - 1, col::SHADOW);
    vline(r.x, r.y, r.h - 1, col::SHADOW);
    hline(r.x + 1, r.y + 1, r.w - 3, col::DKSHADOW);
    vline(r.x + 1, r.y + 1, r.h - 3, col::DKSHADOW);
    hline(r.x, r.bottom() - 1, r.w, col::HILIGHT);
    vline(r.right() - 1, r.y, r.h, col::HILIGHT);
    hline(r.x + 1, r.bottom() - 2, r.w - 2, col::LIGHT);
    vline(r.right() - 2, r.y + 1, r.h - 2, col::LIGHT);
}

void Surface::bevel_pressed(const Rect& r) {
    rect(r, col::DKSHADOW);
    rect(r.inset(1), col::SHADOW);
}

void Surface::bevel_thin_raised(const Rect& r) {
    hline(r.x, r.y, r.w - 1, col::HILIGHT);
    vline(r.x, r.y, r.h - 1, col::HILIGHT);
    hline(r.x, r.bottom() - 1, r.w, col::SHADOW);
    vline(r.right() - 1, r.y, r.h, col::SHADOW);
}

void Surface::bevel_thin_sunken(const Rect& r) {
    hline(r.x, r.y, r.w - 1, col::SHADOW);
    vline(r.x, r.y, r.h - 1, col::SHADOW);
    hline(r.x, r.bottom() - 1, r.w, col::HILIGHT);
    vline(r.right() - 1, r.y, r.h, col::HILIGHT);
}

void Surface::etched(const Rect& r) {
    rect({r.x, r.y, r.w - 1, r.h - 1}, col::SHADOW);
    rect({r.x + 1, r.y + 1, r.w - 1, r.h - 1}, col::HILIGHT);
}

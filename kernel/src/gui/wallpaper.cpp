#include "gui/wallpaper.h"
#include "gui/wm.h"
#include "gui/desktop.h"
#include "apps/bmp.h"
#include "apps/settings.h"
#include "bsl/loader.h"
#include "bsl/images.h"
#include "fs/vfs.h"
#include "sys/paths.h"
#include "lib/i18n.h"
#include "lib/log.h"
#include "lib/string.h"

namespace {
Surface image;          // el fondo guardado (como mucho del tamaño de la pantalla)
bool have = false;
int cur_mode = wallpaper::CENTER;
Surface screen;         // escritorio ya compuesto (color + imagen)
uint32_t screen_bg = 0xFFFFFFFF;
int screen_mode = -1;

// Escala una imagen ARGB a w x h: promedio de áreas al reducir, bilineal al ampliar
void scale(const uint32_t* src, int sw, int sh, int sstride, uint32_t* dst, int dw, int dh, int dstride) {
    for (int y = 0; y < dh; y++) {
        for (int x = 0; x < dw; x++) {
            uint32_t a = 0, r = 0, g = 0, b = 0, n = 0;
            if (sw >= dw && sh >= dh) {
                int x0 = (int)((int64_t)x * sw / dw), x1 = max(x0 + 1, (int)((int64_t)(x + 1) * sw / dw));
                int y0 = (int)((int64_t)y * sh / dh), y1 = max(y0 + 1, (int)((int64_t)(y + 1) * sh / dh));
                int stx = max(1, (x1 - x0) / 8), sty = max(1, (y1 - y0) / 8);
                for (int yy = y0; yy < y1; yy += sty)
                    for (int xx = x0; xx < x1; xx += stx) {
                        uint32_t p = src[(size_t)yy * sstride + xx];
                        uint32_t al = p >> 24;
                        a += al;
                        r += ((p >> 16) & 255) * al;
                        g += ((p >> 8) & 255) * al;
                        b += (p & 255) * al;
                        n++;
                    }
                dst[(size_t)y * dstride + x] = a ? ((a / n) << 24) | ((r / a) << 16) | ((g / a) << 8) | (b / a) : 0;
            } else {
                // bilineal en coma fija (16 bits de fracción)
                int64_t fx = (int64_t)x * ((int64_t)(sw - 1) << 16) / max(1, dw - 1);
                int64_t fy = (int64_t)y * ((int64_t)(sh - 1) << 16) / max(1, dh - 1);
                int ix = (int)(fx >> 16), iy = (int)(fy >> 16);
                uint32_t wx = (uint32_t)(fx & 0xFFFF), wy = (uint32_t)(fy & 0xFFFF);
                int ix1 = min(ix + 1, sw - 1), iy1 = min(iy + 1, sh - 1);
                uint32_t p00 = src[(size_t)iy * sstride + ix], p10 = src[(size_t)iy * sstride + ix1];
                uint32_t p01 = src[(size_t)iy1 * sstride + ix], p11 = src[(size_t)iy1 * sstride + ix1];
                uint32_t out = 0;
                for (int sh2 = 0; sh2 < 32; sh2 += 8) {
                    uint64_t c00 = (p00 >> sh2) & 255, c10 = (p10 >> sh2) & 255, c01 = (p01 >> sh2) & 255, c11 = (p11 >> sh2) & 255;
                    uint64_t top = c00 * (65536 - wx) + c10 * wx, bot = c01 * (65536 - wx) + c11 * wx;
                    uint64_t v = (top * (65536 - wy) + bot * wy) >> 32;
                    out |= (uint32_t)(v & 255) << sh2;
                }
                dst[(size_t)y * dstride + x] = out;
            }
            (void)n;
        }
    }
}

uint32_t over(uint32_t bg, uint32_t p) {
    uint32_t a = p >> 24;
    if (a == 255) return p & 0xFFFFFF;
    if (!a) return bg & 0xFFFFFF;
    return blend(bg, p & 0xFFFFFF, (int)(a + (a >> 7)));
}

// Compone imagen + color en 'dst' (w x h) según el modo; 'img' está a tamaño de pantalla
void compose(Surface& dst, const Surface& img, int mode, uint32_t bg, int sw, int sh) {
    dst.clear(bg);
    if (!img.w || !img.h) return;
    int w = dst.w, h = dst.h;
    // escala de la pantalla real a la superficie de destino (vista previa)
    auto put_scaled = [&](int x0, int y0, int iw, int ih) {
        // dibuja 'img' en el rectángulo (x0, y0, iw, ih) de coordenadas de pantalla
        int dx0 = (int)((int64_t)x0 * w / sw), dy0 = (int)((int64_t)y0 * h / sh);
        int dw = max(1, (int)((int64_t)iw * w / sw)), dh = max(1, (int)((int64_t)ih * h / sh));
        Surface tmp(dw, dh);
        scale(img.px, img.w, img.h, img.stride, tmp.px, dw, dh, tmp.stride);
        for (int y = 0; y < dh; y++) {
            int py = dy0 + y;
            if (py < 0 || py >= h) continue;
            for (int x = 0; x < dw; x++) {
                int px = dx0 + x;
                if (px < 0 || px >= w) continue;
                dst.px[(size_t)py * dst.stride + px] = over(bg, tmp.px[(size_t)y * tmp.stride + x]);
            }
        }
    };
    switch (mode) {
    case wallpaper::TILE:
        for (int y = 0; y < sh; y += img.h)
            for (int x = 0; x < sw; x += img.w) put_scaled(x, y, img.w, img.h);
        break;
    case wallpaper::STRETCH:
        put_scaled(0, 0, sw, sh);
        break;
    case wallpaper::FIT: {
        int iw = sw, ih = (int)((int64_t)img.h * sw / img.w);
        if (ih > sh) { ih = sh; iw = (int)((int64_t)img.w * sh / img.h); }
        put_scaled((sw - iw) / 2, (sh - ih) / 2, iw, ih);
        break;
    }
    default:
        put_scaled((sw - img.w) / 2, (sh - img.h) / 2, img.w, img.h);
    }
}

// Decodifica una imagen a una superficie de, como mucho, el tamaño de la pantalla
bool decode_fit(const char* path, Surface& out, String& err) {
    const ImagesApi* api = bsl::get<ImagesApi>("images", IMAGES_API);
    if (!api) { err = T("Falta la biblioteca del sistema images.bsl."); return false; }
    uint8_t* data;
    uint32_t size;
    if (!vfs::read_file(path, data, size)) { err = vfs::error(); return false; }
    Image im;
    const char* e = "";
    bool ok = api->decode(data, size, im, e);
    mm::kfree(data);
    if (!ok) { err = e; return false; }
    int sw = wm::screen_w(), sh = wm::screen_h();
    int w = im.w, h = im.h;
    if (w > sw || h > sh) {
        // se reduce lo justo para cubrir la pantalla (así sirve para todos los modos)
        int64_t kx = (int64_t)sw * 65536 / w, ky = (int64_t)sh * 65536 / h;
        int64_t k = kx > ky ? kx : ky;
        w = max(1, (int)((int64_t)w * k / 65536));
        h = max(1, (int)((int64_t)h * k / 65536));
    }
    out.resize(w, h);
    scale(im.frames[0].px, im.w, im.h, im.w, out.px, w, h, out.stride);
    api->release(im);
    return true;
}

void invalidate_desktop() {
    screen_mode = -1;
    if (desktop()) desktop()->invalidate();
}
}

bool wallpaper::set(const char* path, int mode, String& err) {
    Surface s;
    if (!decode_fit(path, s, err)) return false;
    // se guarda en BMP (sobre el color del escritorio si tenía transparencia)
    Surface flat(s.w, s.h);
    for (int i = 0; i < s.w * s.h; i++) flat.px[i] = over(settings::desktop_color, s.px[i]);
    Vec<uint8_t> bmp = bmp_encode(flat);
    {
        vfs::SystemWrite sw;
        if (!vfs::write_file(sys::WALLPAPER, bmp.data(), (uint32_t)bmp.size())) { err = vfs::error(); return false; }
    }
    image.resize(s.w, s.h);
    memcpy(image.px, flat.px, (size_t)s.w * s.h * 4);
    for (int i = 0; i < s.w * s.h; i++) image.px[i] |= 0xFF000000u;
    have = true;
    cur_mode = mode;
    settings::wallpaper = true;
    settings::wallpaper_mode = mode;
    invalidate_desktop();
    return true;
}

void wallpaper::set_mode(int mode) {
    cur_mode = mode;
    settings::wallpaper_mode = mode;
    invalidate_desktop();
}

void wallpaper::clear() {
    have = false;
    image.resize(1, 1);
    settings::wallpaper = false;
    invalidate_desktop();
}

bool wallpaper::load() {
    cur_mode = settings::wallpaper_mode;
    if (!settings::wallpaper) return false;
    String err;
    if (!decode_fit(sys::WALLPAPER, image, err)) {
        klog("Fondo: no se puede cargar %s: %s\n", sys::WALLPAPER, err.c_str());
        have = false;
        return false;
    }
    for (int i = 0; i < image.w * image.h; i++) image.px[i] |= 0xFF000000u;
    have = true;
    invalidate_desktop();
    return true;
}

bool wallpaper::active() { return have; }
int wallpaper::mode() { return cur_mode; }

void wallpaper::paint(Surface& s, uint32_t bg) {
    int sw = wm::screen_w(), sh = wm::screen_h();
    if (!have) { s.clear(bg); return; }
    if (screen.w != sw || screen.h != sh || screen_bg != bg || screen_mode != cur_mode) {
        screen.resize(sw, sh);
        compose(screen, image, cur_mode, bg, sw, sh);
        screen_bg = bg;
        screen_mode = cur_mode;
    }
    s.blit(screen, 0, 0);
}

bool wallpaper::load_image(const char* path, Surface& out, String& err) { return decode_fit(path, out, err); }

void wallpaper::compose_preview(Surface& out, const Surface& img, int mode, uint32_t bg) {
    compose(out, img, mode, bg, wm::screen_w(), wm::screen_h());
}

const Surface* wallpaper::current() { return have ? &image : nullptr; }

#include "gfx/gfx.h"
#include "gfx/bootscreen.h"
#include "lib/log.h"
#include "lib/string.h"
#include "boot/bootinfo.h"
#include "mm/memory.h"

namespace {
BootFramebuffer* fb = nullptr;
Surface back;
bool is_ready = false;
bool native32 = false;

BootFramebuffer* get_fb() {
    BootFramebuffer& f = boot_info.fb;
    if (!f.phys || f.bpp < 15) return nullptr;
    if (!f.virt) {
        // antes de tener paginación propia sólo es accesible por el mapa directo (< 4 GiB)
        if (f.phys + (uint64_t)f.pitch * f.height > 0x100000000ull) return nullptr;
        f.virt = (void*)(f.phys + HHDM_BASE);
    }
    return &f;
}

// Superficie que apunta directamente a la memoria de vídeo (sólo 32 bpp)
Surface front;
bool front_ok = false;
bool boot_enabled = true;
int boot_x = 8, boot_y = 8;
}

void gfx::init() {
    BootFramebuffer& bf = boot_info.fb;
    if (!bf.phys) panic("El cargador no ha configurado un modo gráfico");
    // ahora se vuelve a mapear con combinación de escritura (mucho más rápido)
    bf.virt = mm::map_wc(bf.phys, (size_t)bf.pitch * bf.height);
    fb = &bf;
    native32 = fb->bpp == 32 && fb->red_pos == 16 && fb->green_pos == 8 && fb->blue_pos == 0;
    back.resize((int)fb->width, (int)fb->height);
    back.clear(col::BLACK);
    is_ready = true;
    klog("Pantalla: %ux%u, %u bpp, pitch %u%s\n", fb->width, fb->height, fb->bpp, fb->pitch,
         native32 ? "" : " (conversión de formato)");
}

bool gfx::ready() { return is_ready; }
int gfx::width() { return (int)fb->width; }
int gfx::height() { return (int)fb->height; }
Surface& gfx::backbuffer() { return back; }

void gfx::present(const Rect& r0) {
    Rect r = r0.intersect({0, 0, back.w, back.h});
    if (r.empty()) return;
    uint8_t* base = (uint8_t*)fb->virt;
    if (native32) {
        for (int y = r.y; y < r.bottom(); y++)
            memcpy(base + y * fb->pitch + r.x * 4, back.px + y * back.stride + r.x, r.w * 4);
        return;
    }
    int bytes = fb->bpp / 8;
    for (int y = r.y; y < r.bottom(); y++) {
        uint8_t* row = base + y * fb->pitch;
        for (int x = r.x; x < r.right(); x++) {
            uint32_t c = back.px[y * back.stride + x];
            uint32_t rr = (c >> 16) & 255, gg = (c >> 8) & 255, bb = c & 255;
            uint32_t v = ((rr >> (8 - fb->red_size)) << fb->red_pos) |
                         ((gg >> (8 - fb->green_size)) << fb->green_pos) |
                         ((bb >> (8 - fb->blue_size)) << fb->blue_pos);
            for (int b = 0; b < bytes; b++) row[x * bytes + b] = (v >> (8 * b)) & 255;
        }
    }
}

void gfx::present_all() { present({0, 0, back.w, back.h}); }

// --------------------------------------------------------------------------
// Consola de arranque
// --------------------------------------------------------------------------
void bootscreen_init() {
    auto* f = get_fb();
    if (!f || f->bpp != 32) return;
    front.wrap((uint32_t*)f->virt, (int)f->width, (int)f->height, (int)(f->pitch / 4));
    front_ok = true;
    front.clear(0x000000);
    // El registro técnico sólo se muestra en pantalla con la opción "verbose";
    // normalmente va al puerto serie y a la pantalla de carga.
    if (!cmdline_has("verbose")) { boot_enabled = false; return; }
    front.text(font_large, 8, 8, "BrysOS", 0xFFFFFF);
    boot_y = 8 + font_large.height + 8;
}

void bootscreen_log(const char* s) {
    if (!front_ok || !boot_enabled) return;
    for (; *s; s++) {
        if (*s == '\n' || boot_x > front.w - 16) {
            boot_x = 8;
            boot_y += font_mono.height;
            if (boot_y + font_mono.height > front.h) {
                // desplaza hacia arriba
                int lh = font_mono.height;
                for (int y = 40; y < front.h - lh; y++)
                    memcpy(front.px + y * front.stride, front.px + (y + lh) * front.stride, front.w * 4);
                front.fill({0, front.h - lh, front.w, lh}, 0);
                boot_y -= lh;
            }
            if (*s == '\n') continue;
        }
        boot_x += front.text(font_mono, boot_x, boot_y, s, 0xC0C0C0, 1);
    }
}

void bootscreen_disable() { boot_enabled = false; }

void bluescreen(const char* title, const char* msg) {
    auto* f = get_fb();
    if (!f) return;
    Surface s;
    if (f->bpp == 32) s.wrap((uint32_t*)f->virt, (int)f->width, (int)f->height, (int)(f->pitch / 4));
    else if (is_ready) s.wrap(back.px, back.w, back.h, back.stride);
    else return;
    s.clear(0x0000AA);
    const Font& fn = font_mono;
    int cx = s.w / 2;
    int y = s.h / 4;
    char t[96];
    ksnprintf(t, sizeof t, " %s ", title);
    int tw = fn.text_width(t);
    s.fill({cx - tw / 2, y, tw, fn.height}, 0xAAAAAA);
    s.text(fn, cx - tw / 2, y, t, 0x0000AA);
    y += fn.height * 2;
    int x0 = cx - 60 * fn.char_width('M') / 2;
    if (x0 < 8) x0 = 8;
    s.text(fn, x0, y, "Se ha producido un error fatal y BrysOS se ha detenido.", 0xFFFFFF);
    y += fn.height * 2;
    const char* p = msg;
    while (*p) {
        const char* e = p;
        while (*e && *e != '\n') e++;
        s.text(fn, x0, y, p, 0xFFFFFF, (int)(e - p));
        y += fn.height;
        p = *e ? e + 1 : e;
    }
    y += fn.height;
    s.text(fn, x0, y, "Reinicie el equipo para continuar.", 0xFFFFFF);
    if (f->bpp != 32 && is_ready) gfx::present_all();
}

#include "gui/bootsplash.h"
#include "gfx/gfx.h"
#include "drivers/audio.h"
#include "arch/idt.h"
#include "arch/io.h"
#include "lib/log.h"
#include "lib/i18n.h"
#include "lib/string.h"
#include "lib/vec.h"
#include "drivers/input.h"
#include "drivers/usb.h"
#include "drivers/acpi.h"
#include "boot/bootinfo.h"

extern "C" const uint8_t intro_pcm_start[], intro_pcm_end[], logo_data[];

namespace {
bool active = false;
int last_pct = -1;

int logo_w() { return (int)*(const uint32_t*)logo_data; }
int logo_h() { return (int)*(const uint32_t*)(logo_data + 4); }
const uint32_t* logo_px() { return (const uint32_t*)(logo_data + 8); }

// Dibuja el logo sobre fondo negro con una opacidad global (0..256), opcionalmente reducido
void draw_logo(Surface& s, int x0, int y0, int alpha, int scale_div) {
    int w = logo_w(), h = logo_h();
    const uint32_t* px = logo_px();
    for (int y = 0; y < h / scale_div; y++) {
        uint32_t* row = s.px + (y0 + y) * s.stride + x0;
        if (y0 + y < 0 || y0 + y >= s.h) continue;
        for (int x = 0; x < w / scale_div; x++) {
            if (x0 + x < 0 || x0 + x >= s.w) continue;
            uint32_t p = px[(y * scale_div) * w + x * scale_div];
            uint32_t a = ((p >> 24) * (uint32_t)alpha) >> 8;
            uint32_t r = (((p >> 16) & 255) * a) >> 8, g = (((p >> 8) & 255) * a) >> 8, b = ((p & 255) * a) >> 8;
            row[x] = (r << 16) | (g << 8) | b;
        }
    }
}
}

void bootsplash::intro() {
    Surface& s = gfx::backbuffer();
    s.reset_clip();
    s.clear(0);
    gfx::present_all();
    size_t frames = (size_t)(intro_pcm_end - intro_pcm_start) / 4;
    uint64_t duration = frames ? frames * 1000 / 48000 : 8000;
    bool sound = frames && audio::available() && audio::play((const int16_t*)intro_pcm_start, frames, cmdline_int("introvol", 100));
    klog("Presentación: %lu ms%s\n", duration, sound ? " con sonido" : " sin sonido");
    int w = logo_w(), h = logo_h();
    int x0 = (s.w - w) / 2, y0 = (s.h - h) / 2;
    Rect area{x0, y0, w, h};
    constexpr uint64_t FADE_IN = 1200, FADE_OUT = 1500, TAIL = 300;
    uint64_t start = pit::ticks();
    int last_alpha = -1;
    for (;;) {
        uint64_t t = pit::ticks() - start;
        if (t >= duration + TAIL) break;
        int alpha;
        if (t < FADE_IN) alpha = (int)(t * 256 / FADE_IN);
        else if (t + FADE_OUT > duration) alpha = (int)((duration + TAIL - t) * 256 / (FADE_OUT + TAIL));
        else alpha = 256;
        alpha = clamp(alpha, 0, 256);
        if (alpha != last_alpha) {
            draw_logo(s, x0, y0, alpha, 1);
            gfx::present(area);
            last_alpha = alpha;
        }
        hlt();
    }
    if (sound) audio::stop();
    s.fill(area, 0);
    gfx::present(area);
}

void bootsplash::loading_begin() {
    Surface& s = gfx::backbuffer();
    s.reset_clip();
    s.clear(0);
    int lw = logo_w() / 2, lh = logo_h() / 2;
    draw_logo(s, (s.w - lw) / 2, s.h / 2 - lh - 30, 256, 2);
    gfx::present_all();
    active = true;
    last_pct = -1;
}

void bootsplash::loading_step(const char* text, int pct) {
    if (!active) return;
    Surface& s = gfx::backbuffer();
    s.reset_clip();
    int bw = 320, bh = 16;
    Rect bar{(s.w - bw) / 2, s.h / 2 + 10, bw, bh};
    Rect txt{0, bar.bottom() + 14, s.w, font_ui.height + 4};
    // barra de progreso al estilo de la instalación de Windows 98
    s.fill(bar.inset(-2), 0);
    s.rect(bar.inset(-2), 0x606060);
    int filled = (bw - 4) * clamp(pct, 0, 100) / 100;
    s.fill(bar.inset(1), 0x101010);
    for (int x = 0; x < filled; x += 10) s.fill({bar.x + 2 + x, bar.y + 2, min(8, filled - x), bh - 4}, 0x2060E0);
    s.fill(txt, 0);
    s.text_centered(font_ui, txt, text, 0xC0C0C0);
    gfx::present(bar.inset(-2));
    gfx::present(txt);
    if (pct >= 100) active = false;
    last_pct = pct;
}

bool bootsplash::loading_active() { return active; }

void bootsplash::fatal(const char* title, const char* text) {
    Surface& s = gfx::backbuffer();
    s.reset_clip();
    s.clear(0);
    int lw = logo_w() / 2, lh = logo_h() / 2;
    int y = s.h / 2 - lh - 80;
    draw_logo(s, (s.w - lw) / 2, y, 256, 2);
    y += lh + 30;
    s.text_centered(font_ui_bold, {0, y, s.w, 20}, title, 0xFF6060);
    y += 34;
    const char* p = text;
    while (*p) {
        const char* e = p;
        while (*e && *e != '\n') e++;
        String line(p, (size_t)(e - p));
        s.text_centered(font_ui, {0, y, s.w, 16}, line.c_str(), 0xC0C0C0);
        y += 16;
        p = *e ? e + 1 : e;
    }
    // Diagnóstico: últimas líneas del registro sobre los discos (para poder fotografiarlas)
    {
        const char* logp = log_buffer();
        Vec<String> lines;
        while (*logp) {
            const char* e = logp;
            while (*e && *e != '\n') e++;
            String l(logp, (size_t)(e - logp));
            if (str_icontains(l.c_str(), "AHCI") || str_icontains(l.c_str(), "ATA:") || str_icontains(l.c_str(), "clase 01."))
                lines.push(l);
            logp = *e ? e + 1 : e;
        }
        y += 24;
        size_t first = lines.size() > 14 ? lines.size() - 14 : 0;
        for (size_t i = first; i < lines.size(); i++) {
            s.text(font_mono, 40, y, lines[i].c_str(), 0x80C080);
            y += font_mono.height;
        }
    }
    gfx::present_all();
    InputEvent ev;
    while (input::pop(ev)) {}
    for (;;) {
        usb::poll();
        while (input::pop(ev))
            if (ev.type == InputEvent::KEY && ev.pressed) acpi::reboot();
        hlt();
    }
}

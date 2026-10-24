#pragma once
#include <stdint.h>

// Fuente de mapa de bits proporcional, caracteres Latin-1 32..255.
// Cada fila de un glifo es un uint32_t: el bit 31 es el píxel más a la izquierda.
struct Font {
    uint8_t height;
    uint8_t ascent;
    uint8_t max_width;
    const uint32_t* bits;   // [224][height]
    const uint8_t* widths;  // [224]

    int char_width(uint8_t c) const {
        if (c < 32) return 0;
        return widths[c - 32];
    }
    const uint32_t* glyph(uint8_t c) const {
        if (c < 32) c = '?';
        return bits + (c - 32) * height;
    }
    int text_width(const char* s, int len = -1) const {
        int w = 0;
        for (int i = 0; s[i] && (len < 0 || i < len); i++) w += char_width((uint8_t)s[i]);
        return w;
    }
};

extern const Font font_ui;
extern const Font font_ui_bold;
extern const Font font_mono;
extern const Font font_large;

#include "drivers/keyboard.h"
#include "drivers/input.h"
#include "lib/string.h"
#include "arch/io.h"

namespace {
// Caracteres especiales para teclas muertas
constexpr uint8_t DEAD_ACUTE = 1, DEAD_GRAVE = 2, DEAD_CIRC = 3, DEAD_DIAER = 4, DEAD_TILDE = 5;

struct KeyDef { uint8_t normal, shift, altgr; };
KeyDef keymap[128];
int current = 0;
void (*led_hooks[4])(bool) = {};

const keyboard::LayoutInfo layouts[] = {
    {"es", "Español (España)"},
    {"latam", "Español (Latinoamérica)"},
    {"us", "Inglés (Estados Unidos)"},
};

void km(uint8_t sc, uint8_t n, uint8_t s, uint8_t a = 0) { keymap[sc] = {n, s, a}; }

void common_letters() {
    memset(keymap, 0, sizeof keymap);
    const char* row1 = "qwertyuiop";
    for (int i = 0; i < 10; i++) km(0x10 + i, row1[i], row1[i] - 32);
    const char* row2 = "asdfghjkl";
    for (int i = 0; i < 9; i++) km(0x1E + i, row2[i], row2[i] - 32);
    const char* row3 = "zxcvbnm";
    for (int i = 0; i < 7; i++) km(0x2C + i, row3[i], row3[i] - 32);
    km(0x39, ' ', ' ');
    km(0x37, '*', '*'); // teclado numérico
    km(0x4A, '-', '-');
    km(0x4E, '+', '+');
}

void build_es() {
    common_letters();
    km(0x29, 0xBA /*º*/, 0xAA /*ª*/, '\\');
    km(0x02, '1', '!', '|');
    km(0x03, '2', '"', '@');
    km(0x04, '3', 0xB7 /*·*/, '#');
    km(0x05, '4', '$', '~');
    km(0x06, '5', '%');
    km(0x07, '6', '&', 0xAC /*¬*/);
    km(0x08, '7', '/');
    km(0x09, '8', '(');
    km(0x0A, '9', ')');
    km(0x0B, '0', '=');
    km(0x0C, '\'', '?');
    km(0x0D, 0xA1 /*¡*/, 0xBF /*¿*/);
    km(0x1A, DEAD_GRAVE, DEAD_CIRC, '[');
    km(0x1B, '+', '*', ']');
    km(0x27, 0xF1 /*ñ*/, 0xD1 /*Ñ*/);
    km(0x28, DEAD_ACUTE, DEAD_DIAER, '{');
    km(0x2B, 0xE7 /*ç*/, 0xC7 /*Ç*/, '}');
    km(0x33, ',', ';');
    km(0x34, '.', ':');
    km(0x35, '-', '_');
    km(0x56, '<', '>');
}

void build_latam() {
    common_letters();
    km(0x29, '|', 0xB0 /*°*/, 0xAC /*¬*/);
    km(0x02, '1', '!');
    km(0x03, '2', '"');
    km(0x04, '3', '#');
    km(0x05, '4', '$');
    km(0x06, '5', '%');
    km(0x07, '6', '&');
    km(0x08, '7', '/');
    km(0x09, '8', '(');
    km(0x0A, '9', ')');
    km(0x0B, '0', '=');
    km(0x0C, '\'', '?', '\\');
    km(0x0D, 0xBF /*¿*/, 0xA1 /*¡*/);
    keymap[0x10].altgr = '@';
    km(0x1A, DEAD_ACUTE, DEAD_DIAER);
    km(0x1B, '+', '*', '~');
    km(0x27, 0xF1 /*ñ*/, 0xD1 /*Ñ*/);
    km(0x28, '{', '[', DEAD_CIRC);
    km(0x2B, '}', ']', DEAD_GRAVE);
    km(0x33, ',', ';');
    km(0x34, '.', ':');
    km(0x35, '-', '_');
    km(0x56, '<', '>');
}

void build_us() {
    common_letters();
    km(0x29, '`', '~');
    const char* num = "1234567890";
    const char* sh = "!@#$%^&*()";
    for (int i = 0; i < 10; i++) km(0x02 + i, num[i], sh[i]);
    km(0x0C, '-', '_');
    km(0x0D, '=', '+');
    km(0x1A, '[', '{');
    km(0x1B, ']', '}');
    km(0x27, ';', ':');
    km(0x28, '\'', '"');
    km(0x2B, '\\', '|');
    km(0x33, ',', '<');
    km(0x34, '.', '>');
    km(0x35, '/', '?');
    km(0x56, '\\', '|');
}

uint8_t compose(uint8_t dead, uint8_t c) {
    const char* base = "aeiouAEIOU";
    static const uint8_t acute[] = {0xE1, 0xE9, 0xED, 0xF3, 0xFA, 0xC1, 0xC9, 0xCD, 0xD3, 0xDA};
    static const uint8_t grave[] = {0xE0, 0xE8, 0xEC, 0xF2, 0xF9, 0xC0, 0xC8, 0xCC, 0xD2, 0xD9};
    static const uint8_t circ[] = {0xE2, 0xEA, 0xEE, 0xF4, 0xFB, 0xC2, 0xCA, 0xCE, 0xD4, 0xDB};
    static const uint8_t diaer[] = {0xE4, 0xEB, 0xEF, 0xF6, 0xFC, 0xC4, 0xCB, 0xCF, 0xD6, 0xDC};
    if (dead == DEAD_TILDE) {
        if (c == 'n') return 0xF1;
        if (c == 'N') return 0xD1;
        if (c == 'a') return 0xE3;
        if (c == 'o') return 0xF5;
        return 0;
    }
    if (dead == DEAD_DIAER && c == 'y') return 0xFF;
    if (dead == DEAD_ACUTE && c == 'y') return 0xFD;
    for (int i = 0; i < 10; i++) {
        if (c == (uint8_t)base[i]) {
            switch (dead) {
            case DEAD_ACUTE: return acute[i];
            case DEAD_GRAVE: return grave[i];
            case DEAD_CIRC: return circ[i];
            case DEAD_DIAER: return diaer[i];
            }
        }
    }
    return 0;
}

uint8_t dead_char(uint8_t dead) {
    switch (dead) {
    case DEAD_ACUTE: return 0xB4; // ´
    case DEAD_GRAVE: return '`';
    case DEAD_CIRC: return '^';
    case DEAD_DIAER: return 0xA8; // ¨
    case DEAD_TILDE: return '~';
    }
    return 0;
}

uint8_t mods = 0;
bool lshift = false, rshift = false, lctrl = false, rctrl = false;
bool e0 = false;
uint8_t pending_dead = 0;
int e1_skip = 0;

uint16_t extended_key(uint8_t sc) {
    switch (sc) {
    case 0x48: return KEY_UP;
    case 0x50: return KEY_DOWN;
    case 0x4B: return KEY_LEFT;
    case 0x4D: return KEY_RIGHT;
    case 0x47: return KEY_HOME;
    case 0x4F: return KEY_END;
    case 0x49: return KEY_PGUP;
    case 0x51: return KEY_PGDN;
    case 0x52: return KEY_INSERT;
    case 0x53: return KEY_DELETE;
    case 0x1C: return KEY_ENTER;
    case 0x1D: return KEY_RCTRL;
    case 0x38: return KEY_ALTGR;
    case 0x5B: return KEY_LWIN;
    case 0x5C: return KEY_RWIN;
    case 0x5D: return KEY_MENU;
    case 0x35: return '/';
    case 0x37: return KEY_PRINTSCREEN;
    case 0x30: return KEY_VOLUME_UP;     // teclas multimedia (también las del HP Pavilion)
    case 0x2E: return KEY_VOLUME_DOWN;
    case 0x20: return KEY_MUTE;
    }
    return KEY_NONE;
}

uint16_t plain_key(uint8_t sc) {
    switch (sc) {
    case 0x01: return KEY_ESC;
    case 0x0E: return KEY_BACKSPACE;
    case 0x0F: return KEY_TAB;
    case 0x1C: return KEY_ENTER;
    case 0x1D: return KEY_LCTRL;
    case 0x2A: return KEY_LSHIFT;
    case 0x36: return KEY_RSHIFT;
    case 0x38: return KEY_LALT;
    case 0x3A: return KEY_CAPSLOCK;
    case 0x45: return KEY_NUMLOCK;
    case 0x46: return KEY_SCROLLLOCK;
    case 0x57: return KEY_F11;
    case 0x58: return KEY_F12;
    }
    if (sc >= 0x3B && sc <= 0x44) return KEY_F1 + (sc - 0x3B);
    // Teclado numérico sin Bloq Num: actúa como flechas
    switch (sc) {
    case 0x47: return KEY_HOME;
    case 0x48: return KEY_UP;
    case 0x49: return KEY_PGUP;
    case 0x4B: return KEY_LEFT;
    case 0x4D: return KEY_RIGHT;
    case 0x4F: return KEY_END;
    case 0x50: return KEY_DOWN;
    case 0x51: return KEY_PGDN;
    case 0x52: return KEY_INSERT;
    case 0x53: return KEY_DELETE;
    }
    return KEY_NONE;
}
}

int keyboard::layout_count() { return (int)(sizeof layouts / sizeof layouts[0]); }
const keyboard::LayoutInfo& keyboard::layout(int i) { return layouts[i]; }
const char* keyboard::layout_id() { return layouts[current].id; }
void keyboard::set_led_hook(void (*fn)(bool)) {
    for (auto& h : led_hooks) if (!h) { h = fn; return; }
}

bool keyboard::set_layout(const char* id) {
    for (int i = 0; i < layout_count(); i++) {
        if (strcmp(layouts[i].id, id)) continue;
        IrqGuard g;
        current = i;
        pending_dead = 0;
        if (i == 0) build_es();
        else if (i == 1) build_latam();
        else build_us();
        return true;
    }
    return false;
}

void keyboard::scancode(uint8_t b) {
    if (!keymap[0x10].normal) set_layout("es");
    if (b == 0xFE || b == 0x00 || b == 0xFF) return; // 0xAA no: es soltar Mayús izquierda
    if (e1_skip) { e1_skip--; return; }
    if (b == 0xE1) {
        e1_skip = 5; // Pausa: E1 1D 45 E1 9D C5
        InputEvent e;
        e.type = InputEvent::KEY;
        e.pressed = true;
        e.key = KEY_PAUSE;
        input::push(e);
        return;
    }
    if (b == 0xE0) { e0 = true; return; }

    bool released = b & 0x80;
    uint8_t sc = b & 0x7F;
    bool ext = e0;
    e0 = false;
    if (ext && (sc == 0x2A || sc == 0x36)) return; // falsos Shift de Impr Pant

    InputEvent e;
    e.type = InputEvent::KEY;
    e.pressed = !released;

    uint16_t key = ext ? extended_key(sc) : plain_key(sc);
    // Con distribución de EE. UU., Alt derecho es un Alt normal
    if (key == KEY_ALTGR && current == 2) key = KEY_LALT;
    switch (key) {
    case KEY_LSHIFT: lshift = !released; break;
    case KEY_RSHIFT: rshift = !released; break;
    case KEY_LCTRL: lctrl = !released; break;
    case KEY_RCTRL: rctrl = !released; break;
    case KEY_LALT: if (released) mods &= ~MOD_ALT; else mods |= MOD_ALT; break;
    case KEY_ALTGR: if (released) mods &= ~MOD_ALTGR; else mods |= MOD_ALTGR; break;
    case KEY_CAPSLOCK:
        if (!released) {
            mods ^= MOD_CAPS;
            for (auto h : led_hooks) if (h) h(mods & MOD_CAPS);
        }
        break;
    default: break;
    }
    mods = (mods & ~(MOD_SHIFT | MOD_CTRL)) | ((lshift || rshift) ? MOD_SHIFT : 0) | ((lctrl || rctrl) ? MOD_CTRL : 0);
    e.mods = mods;

    if (key == KEY_NONE && !ext && sc < 128) {
        KeyDef kd = keymap[sc];
        if (!kd.normal) return;
        bool shift = mods & MOD_SHIFT;
        bool is_letter = (kd.normal >= 'a' && kd.normal <= 'z') || kd.normal == 0xF1 || kd.normal == 0xE7;
        if (is_letter && (mods & MOD_CAPS)) shift = !shift;
        uint8_t c = (mods & MOD_ALTGR) ? kd.altgr : (shift ? kd.shift : kd.normal);
        // Código de tecla estable: la letra en minúscula o el carácter sin modificar
        e.key = (kd.normal >= DEAD_ACUTE && kd.normal <= DEAD_TILDE) ? (uint16_t)(0x200 + sc) : kd.normal;
        if (released) { input::push(e); return; }
        if (mods & MOD_CTRL) { e.ch = 0; input::push(e); return; }
        if (c >= DEAD_ACUTE && c <= DEAD_TILDE) { pending_dead = c; return; }
        if (c && pending_dead) {
            uint8_t comp = compose(pending_dead, c);
            if (!comp) {
                // Tecla muerta sin combinación: emite el acento y luego el carácter
                InputEvent d = e;
                d.ch = c == ' ' ? 0 : dead_char(pending_dead);
                if (d.ch) input::push(d);
                if (c == ' ') c = dead_char(pending_dead);
            } else {
                c = comp;
            }
            pending_dead = 0;
        }
        e.ch = c;
        input::push(e);
        return;
    }
    if (key == KEY_NONE) return;
    e.key = key;
    if (key == KEY_ENTER) e.ch = '\r';
    if (key == KEY_TAB) e.ch = '\t';
    if (key == KEY_BACKSPACE) e.ch = 8;
    if (key == KEY_ESC) e.ch = 27;
    if (key == '/' && !released) e.ch = '/';
    input::push(e);
}

uint8_t input::modifiers() { return mods; }

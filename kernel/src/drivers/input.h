#pragma once
#include <stdint.h>

// Códigos de teclas especiales (los caracteres imprimibles usan su valor Latin-1)
enum Key : uint16_t {
    KEY_NONE = 0,
    KEY_BACKSPACE = 8, KEY_TAB = 9, KEY_ENTER = 13, KEY_ESC = 27,
    KEY_SPECIAL = 0x100,
    KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT,
    KEY_HOME, KEY_END, KEY_PGUP, KEY_PGDN, KEY_INSERT, KEY_DELETE,
    KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,
    KEY_LSHIFT, KEY_RSHIFT, KEY_LCTRL, KEY_RCTRL, KEY_LALT, KEY_ALTGR,
    KEY_CAPSLOCK, KEY_NUMLOCK, KEY_SCROLLLOCK, KEY_LWIN, KEY_RWIN, KEY_MENU,
    KEY_PRINTSCREEN, KEY_PAUSE,
    KEY_VOLUME_UP, KEY_VOLUME_DOWN, KEY_MUTE,   // teclas multimedia
};

enum Mod : uint8_t {
    MOD_SHIFT = 1, MOD_CTRL = 2, MOD_ALT = 4, MOD_ALTGR = 8, MOD_CAPS = 16,
};

enum MouseButton : uint8_t { MB_LEFT = 1, MB_RIGHT = 2, MB_MIDDLE = 4 };

struct InputEvent {
    enum Type : uint8_t { NONE, KEY, MOUSE } type = NONE;
    // Teclado
    bool pressed = false;
    uint16_t key = 0;   // Key o carácter (en minúscula / sin modificar para letras)
    uint8_t ch = 0;     // carácter Latin-1 resultante (0 si no imprimible)
    uint8_t mods = 0;
    // Ratón (movimiento relativo)
    int16_t dx = 0, dy = 0, wheel = 0;
    uint8_t buttons = 0;
};

namespace input {
void push(const InputEvent& e);
bool pop(InputEvent& e);
uint8_t modifiers();
}

namespace ps2 {
void init();
// Llamar periódicamente: gestiona los toques diferidos del touchpad
void poll();
bool has_touchpad();
const char* pointer_name();
}

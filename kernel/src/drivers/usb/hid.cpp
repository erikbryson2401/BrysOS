// Teclado y ratón USB en modo de arranque (boot protocol)
#include "drivers/usb/usb_internal.h"
#include "drivers/keyboard.h"
#include "drivers/input.h"
#include "arch/idt.h"
#include "arch/io.h"
#include "lib/string.h"
#include "lib/log.h"

namespace {
// Uso HID -> código del conjunto 1 (0xE0xx = extendido)
const uint16_t usage_to_set1[0x66] = {
    0, 0, 0, 0,
    0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32, 0x31, 0x18, 0x19,   // a..p
    0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C,                                       // q..z
    0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,                                       // 1..0
    0x1C, 0x01, 0x0E, 0x0F, 0x39,                                                                     // Intro Esc Retroceso Tab Espacio
    0x0C, 0x0D, 0x1A, 0x1B, 0x2B, 0x2B, 0x27, 0x28, 0x29, 0x33, 0x34, 0x35, 0x3A,                     // - = [ ] \ # ; ' ` , . / BloqMayús
    0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43, 0x44, 0x57, 0x58,                           // F1..F12
    0xE037, 0x46, 0, 0xE052, 0xE047, 0xE049, 0xE053, 0xE04F, 0xE051, 0xE04D, 0xE04B, 0xE050, 0xE048,  // ImprPant..Arriba
    0x45, 0xE035, 0x37, 0x4A, 0x4E, 0xE01C, 0x4F, 0x50, 0x51, 0x4B, 0x4C, 0x4D, 0x47, 0x48, 0x49, 0x52, 0x53, // teclado numérico
    0x56, 0xE05D,                                                                                    // tecla < > y Menú
};
const uint16_t modifier_set1[8] = {0x1D, 0x2A, 0x38, 0xE05B, 0xE01D, 0x36, 0xE038, 0xE05C};

void send(uint16_t code, bool release) {
    if (!code) return;
    IrqGuard g;   // el decodificador también se usa desde la IRQ del PS/2
    if (code & 0xFF00) keyboard::scancode(0xE0);
    keyboard::scancode((uint8_t)((code & 0x7F) | (release ? 0x80 : 0)));
}

uint16_t code_of(uint8_t usage) {
    if (usage < sizeof usage_to_set1 / sizeof usage_to_set1[0]) return usage_to_set1[usage];
    if (usage == 0x87) return 0x73;   // tecla internacional (Ro)
    if (usage == 0x7F) return 0xE020; // silencio
    if (usage == 0x80) return 0xE030; // subir volumen
    if (usage == 0x81) return 0xE02E; // bajar volumen
    return 0;
}

// Repetición automática (los teclados USB no la hacen por sí mismos)
uint16_t repeat_code = 0;
uint64_t repeat_at = 0;

void keyboard_report(usb::Device& d, const uint8_t* r, int len) {
    if (len < 3) return;
    if (r[2] == 1) return;   // ErrorRollOver: demasiadas teclas
    uint8_t old_mod = d.last[0], mod = r[0];
    for (int b = 0; b < 8; b++) {
        bool was = old_mod & (1 << b), now = mod & (1 << b);
        if (was != now) send(modifier_set1[b], !now);
    }
    // teclas soltadas
    for (int i = 2; i < 8; i++) {
        uint8_t k = d.last[i];
        if (k < 4) continue;
        bool still = false;
        for (int j = 2; j < len && j < 8; j++) if (r[j] == k) still = true;
        if (!still) {
            send(code_of(k), true);
            if (code_of(k) == repeat_code) repeat_code = 0;
        }
    }
    // teclas pulsadas
    for (int j = 2; j < len && j < 8; j++) {
        uint8_t k = r[j];
        if (k < 4) continue;
        bool was = false;
        for (int i = 2; i < 8; i++) if (d.last[i] == k) was = true;
        if (was) continue;
        if (k == 0x48) {   // Pausa: secuencia E1 1D 45 E1 9D C5
            IrqGuard g;
            const uint8_t seq[] = {0xE1, 0x1D, 0x45, 0xE1, 0x9D, 0xC5};
            for (uint8_t c : seq) keyboard::scancode(c);
            continue;
        }
        uint16_t c = code_of(k);
        send(c, false);
        // las teclas de bloqueo no se repiten
        if (c && c != 0x3A && c != 0x45 && c != 0x46) {
            repeat_code = c;
            repeat_at = pit::ticks() + 500;
        }
    }
    memset(d.last, 0, sizeof d.last);
    memcpy(d.last, r, (size_t)min(len, 8));
}

void mouse_report(const uint8_t* r, int len) {
    if (len < 3) return;
    InputEvent e;
    e.type = InputEvent::MOUSE;
    e.buttons = (r[0] & 1 ? MB_LEFT : 0) | (r[0] & 2 ? MB_RIGHT : 0) | (r[0] & 4 ? MB_MIDDLE : 0);
    e.dx = (int8_t)r[1];
    e.dy = (int8_t)r[2];
    e.wheel = len >= 4 ? -(int8_t)r[3] : 0;
    input::push(e);
}
}

void usb::hid_report(Device& d, const uint8_t* data, int len) {
    if (!d.seen) {
        klog("USB: primeros datos de %s\n", d.desc);
        d.seen = true;
    }
    if (d.kind == 1) keyboard_report(d, data, len);
    else if (d.kind == 2) mouse_report(data, len);
}

void usb::hid_tick() {
    if (repeat_code && pit::ticks() >= repeat_at) {
        send(repeat_code, false);
        repeat_at = pit::ticks() + 33;
    }
}

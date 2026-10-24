// Controlador i8042: teclado PS/2 (distribución española) y ratón PS/2 /
// touchpad Synaptics (modo absoluto con toque para clic, arrastre y desplazamiento
// en el borde derecho; si no se detecta un Synaptics se usa el protocolo de ratón
// estándar o IntelliMouse con rueda).
#include "drivers/input.h"
#include "drivers/keyboard.h"
#include "arch/idt.h"
#include "arch/io.h"
#include "lib/log.h"
#include "lib/string.h"

// ---------------------------------------------------------------------------
// Cola de eventos
// ---------------------------------------------------------------------------
namespace {
constexpr int QSIZE = 256;
InputEvent queue[QSIZE];
volatile int q_head = 0, q_tail = 0;
}

void input::push(const InputEvent& e) {
    IrqGuard g;
    int next = (q_head + 1) % QSIZE;
    if (next == q_tail) return; // cola llena: se descarta
    queue[q_head] = e;
    q_head = next;
}

bool input::pop(InputEvent& e) {
    IrqGuard g;
    if (q_tail == q_head) return false;
    e = queue[q_tail];
    q_tail = (q_tail + 1) % QSIZE;
    return true;
}

// ---------------------------------------------------------------------------
// Bajo nivel del i8042
// ---------------------------------------------------------------------------
namespace {
constexpr uint16_t DATA = 0x60, STATUS = 0x64, CMD = 0x64;

bool wait_write() {
    for (uint64_t t = pit::ticks() + 200; pit::ticks() < t;)
        if (!(inb(STATUS) & 2)) return true;
    return false;
}

int read_data(uint64_t timeout_ms = 200) {
    for (uint64_t t = pit::ticks() + timeout_ms; pit::ticks() < t;)
        if (inb(STATUS) & 1) return inb(DATA);
    return -1;
}

void ctrl_cmd(uint8_t c) { wait_write(); outb(CMD, c); }
void write_data(uint8_t d) { wait_write(); outb(DATA, d); }
void flush() { for (int i = 0; i < 64 && (inb(STATUS) & 1); i++) { inb(DATA); io_wait(); } }

bool dev_write(bool aux, uint8_t b) {
    for (int tries = 0; tries < 3; tries++) {
        if (aux) ctrl_cmd(0xD4);
        write_data(b);
        int r = read_data(300);
        if (r == 0xFA) return true;
        if (r != 0xFE) return false;
    }
    return false;
}
bool mouse_write(uint8_t b) { return dev_write(true, b); }
bool mouse_write2(uint8_t a, uint8_t b) { return mouse_write(a) && mouse_write(b); }

// ---------------------------------------------------------------------------
// Teclado: los bytes se pasan al decodificador común (drivers/keyboard.cpp)
// ---------------------------------------------------------------------------
enum { LED_IDLE, LED_WAIT_ACK } led_state = LED_IDLE;
uint8_t led_value = 0;
bool kbd_present = true;

void ps2_set_leds(bool caps) {
    if (!kbd_present) return;
    led_value = caps ? 4 : 0;
    led_state = LED_WAIT_ACK;
    write_data(0xED);
}

void keyboard_byte(uint8_t b) {
    if (b == 0xFA) {
        if (led_state == LED_WAIT_ACK) { led_state = LED_IDLE; write_data(led_value); }
        return;
    }
    keyboard::scancode(b);
}

void kbd_irq(Frame*) {
    // Puede haber bytes del ratón en el búfer si llegan entrelazados
    uint8_t st = inb(STATUS);
    if (!(st & 1)) return;
    if (st & 0x20) return; // pertenece al ratón; lo recogerá IRQ12
    keyboard_byte(inb(DATA));
}

// ---------------------------------------------------------------------------
// Ratón / touchpad
// ---------------------------------------------------------------------------
enum class Ptr { NONE, PS2, WHEEL, SYNAPTICS } ptr_type = Ptr::NONE;
uint8_t packet[6];
int pkt_idx = 0;
int pkt_len = 3;
uint8_t synaptics_major = 0, synaptics_minor = 0;

void emit_mouse(int dx, int dy, int wheel, uint8_t buttons) {
    InputEvent e;
    e.type = InputEvent::MOUSE;
    e.dx = (int16_t)dx;
    e.dy = (int16_t)dy;
    e.wheel = (int16_t)wheel;
    e.buttons = buttons;
    input::push(e);
}

// --- Estado del touchpad Synaptics -------------------------------------------
constexpr int Z_TOUCH = 30;       // presión mínima para considerar contacto
constexpr int TAP_MS = 180;       // duración máxima de un toque
constexpr int TAP_MOVE = 120;     // desplazamiento máximo (unidades del touchpad)
int tp_xmin = 1472, tp_xmax = 5472, tp_ymin = 1408, tp_ymax = 4448;
bool tp_touching = false;
int tp_lastx = 0, tp_lasty = 0;
int tp_startx = 0, tp_starty = 0;
uint64_t tp_touch_start = 0;
int tp_accx = 0, tp_accy = 0;     // restos de la división para no perder precisión
bool tp_scrolling = false;
int tp_scroll_acc = 0;
// Toques: un toque pendiente se convierte en clic si no hay otro contacto pronto
bool tap_pending = false;
uint64_t tap_time = 0;
bool tap_drag = false;            // tocar-y-arrastrar en curso
uint8_t tp_phys_buttons = 0;
uint8_t tp_reported = 0;
int tp_packets_since_touch = 0;

uint8_t tp_buttons() {
    uint8_t b = tp_phys_buttons;
    if (tap_drag) b |= MB_LEFT;
    return b;
}

void tp_report(int dx, int dy, int wheel) {
    uint8_t b = tp_buttons();
    if (dx || dy || wheel || b != tp_reported) {
        emit_mouse(dx, dy, wheel, b);
        tp_reported = b;
    }
}

void synaptics_packet() {
    int x = ((packet[3] & 0x10) << 8) | ((packet[1] & 0x0F) << 8) | packet[4];
    int y = ((packet[3] & 0x20) << 7) | ((packet[1] & 0xF0) << 4) | packet[5];
    int z = packet[2];
    tp_phys_buttons = (packet[0] & 1 ? MB_LEFT : 0) | (packet[0] & 2 ? MB_RIGHT : 0);
    uint64_t now = pit::ticks();

    if (z >= Z_TOUCH && x > 1) {
        // Ajuste dinámico de los límites reales del touchpad
        if (x < tp_xmin) tp_xmin = x;
        if (x > tp_xmax) tp_xmax = x;
        if (y < tp_ymin) tp_ymin = y;
        if (y > tp_ymax) tp_ymax = y;

        if (!tp_touching) {
            tp_touching = true;
            tp_lastx = tp_startx = x;
            tp_lasty = tp_starty = y;
            tp_touch_start = now;
            tp_accx = tp_accy = 0;
            tp_packets_since_touch = 0;
            // Zona de desplazamiento: franja del 7% en el borde derecho
            tp_scrolling = x > tp_xmax - (tp_xmax - tp_xmin) * 7 / 100;
            tp_scroll_acc = 0;
            if (tap_pending && now - tap_time < TAP_MS) {
                // Segundo contacto justo después de un toque: se entrega el clic
                // del primero y se mantiene el botón (doble clic o arrastre)
                tap_pending = false;
                emit_mouse(0, 0, 0, tp_phys_buttons | MB_LEFT);
                emit_mouse(0, 0, 0, tp_phys_buttons);
                tp_reported = tp_phys_buttons;
                tap_drag = true;
            }
            tp_report(0, 0, 0);
            return;
        }
        tp_packets_since_touch++;
        int dx = x - tp_lastx;
        int dy = y - tp_lasty;
        tp_lastx = x;
        tp_lasty = y;
        // Descarta los primeros paquetes (posición inestable al apoyar el dedo)
        if (tp_packets_since_touch < 2) return;
        if (tp_scrolling) {
            tp_scroll_acc += dy;
            int steps = tp_scroll_acc / 90;
            if (steps) {
                tp_scroll_acc -= steps * 90;
                tp_report(0, 0, -steps); // dedo hacia arriba = rueda hacia arriba
            }
            return;
        }
        // Aceleración simple según la velocidad
        // (en medios pasos: 2 = x1, 3 = x1,5, 4 = x2)
        int speed = abs_val(dx) + abs_val(dy);
        int num = speed > 80 ? 4 : (speed > 40 ? 3 : 2);
        tp_accx += dx * num;
        tp_accy += -dy * num; // el eje Y del touchpad crece hacia arriba
        // unas 4000 unidades de ancho: un barrido completo mueve 400-800 píxeles
        int ox = tp_accx / 20, oy = tp_accy / 20;
        tp_accx -= ox * 20;
        tp_accy -= oy * 20;
        tp_report(ox, oy, 0);
    } else {
        if (tp_touching) {
            tp_touching = false;
            int moved = abs_val(tp_lastx - tp_startx) + abs_val(tp_lasty - tp_starty);
            if (tap_drag) {
                tap_drag = false;
            } else if (now - tp_touch_start < TAP_MS && moved < TAP_MOVE && !tp_phys_buttons && !tp_scrolling) {
                tap_pending = true;
                tap_time = now;
            }
        }
        tp_report(0, 0, 0);
    }
}

void mouse_byte(uint8_t b) {
    if (ptr_type == Ptr::SYNAPTICS) {
        // Sincronización: byte 1 = 10xx0xxx, byte 4 = 11xx0xxx
        if (pkt_idx == 0 && (b & 0xC8) != 0x80) return;
        if (pkt_idx == 3 && (b & 0xC8) != 0xC0) { pkt_idx = 0; return; }
        packet[pkt_idx++] = b;
        if (pkt_idx == 6) { pkt_idx = 0; synaptics_packet(); }
        return;
    }
    if (pkt_idx == 0 && !(b & 0x08)) return; // bit 3 siempre a 1 en el primer byte
    packet[pkt_idx++] = b;
    if (pkt_idx < pkt_len) return;
    pkt_idx = 0;
    uint8_t f = packet[0];
    if (f & 0xC0) return; // desbordamiento
    int dx = (int)packet[1] - ((f & 0x10) ? 256 : 0);
    int dy = (int)packet[2] - ((f & 0x20) ? 256 : 0);
    int wheel = 0;
    if (pkt_len == 4) wheel = (int8_t)(packet[3] << 4) >> 4;
    uint8_t buttons = (f & 1 ? MB_LEFT : 0) | (f & 2 ? MB_RIGHT : 0) | (f & 4 ? MB_MIDDLE : 0);
    emit_mouse(dx, -dy, wheel, buttons);
}

void mouse_irq(Frame*) {
    uint8_t st = inb(STATUS);
    if (!(st & 1)) return;
    uint8_t b = inb(DATA);
    if (!(st & 0x20)) { keyboard_byte(b); return; }
    mouse_byte(b);
}

// Envía un argumento de 8 bits codificado con 4 órdenes "Set Resolution"
bool synaptics_special(uint8_t arg) {
    if (!mouse_write(0xE6)) return false;
    for (int i = 6; i >= 0; i -= 2)
        if (!mouse_write2(0xE8, (arg >> i) & 3)) return false;
    return true;
}

bool synaptics_query(uint8_t q, uint8_t out[3]) {
    if (!synaptics_special(q) || !mouse_write(0xE9)) return false;
    for (int i = 0; i < 3; i++) {
        int r = read_data(300);
        if (r < 0) return false;
        out[i] = (uint8_t)r;
    }
    return true;
}

bool synaptics_set_mode(uint8_t mode) {
    return synaptics_special(mode) && mouse_write2(0xF3, 0x14);
}

bool mouse_reset() {
    if (!mouse_write(0xFF)) return false;
    int a = read_data(1500); // autotest (0xAA)
    int b = read_data(300);  // identificador (0x00)
    return a == 0xAA && (b == 0x00 || b < 0);
}

int mouse_id() {
    if (!mouse_write(0xF2)) return -1;
    return read_data(300);
}

void detect_pointer() {
    if (!mouse_reset()) {
        klog("PS/2: no hay ratón ni touchpad en el puerto auxiliar\n");
        return;
    }
    uint8_t id[3];
    if (synaptics_query(0x00, id) && id[1] == 0x47) {
        synaptics_minor = id[0];
        synaptics_major = id[2] & 0x0F;
        klog("PS/2: touchpad Synaptics v%u.%u detectado\n", synaptics_major, synaptics_minor);
        uint8_t caps[3];
        if (synaptics_query(0x02, caps))
            klog("PS/2: capacidades Synaptics %02x %02x %02x\n", caps[0], caps[1], caps[2]);
        // Modo absoluto (bit 7) + 80 paquetes/s (bit 6)
        if (synaptics_set_mode(0xC0) && mouse_write2(0xE8, 0x03)) {
            ptr_type = Ptr::SYNAPTICS;
            pkt_len = 6;
            return;
        }
        klog("PS/2: no se pudo activar el modo absoluto, se usa modo ratón\n");
        mouse_reset();
    }
    // Secuencia mágica de IntelliMouse para activar la rueda
    mouse_write2(0xF3, 200);
    mouse_write2(0xF3, 100);
    mouse_write2(0xF3, 80);
    int mid = mouse_id();
    if (mid == 3) {
        ptr_type = Ptr::WHEEL;
        pkt_len = 4;
        klog("PS/2: ratón con rueda (IntelliMouse)\n");
    } else {
        ptr_type = Ptr::PS2;
        pkt_len = 3;
        klog("PS/2: ratón estándar (id %d)\n", mid);
    }
    mouse_write2(0xF3, 100);
    mouse_write2(0xE8, 0x03);
}
}

void ps2::init() {
    keyboard::set_led_hook(ps2_set_leds);
    ctrl_cmd(0xAD); // deshabilita teclado
    ctrl_cmd(0xA7); // deshabilita puerto auxiliar
    flush();

    ctrl_cmd(0x20);
    int cfg = read_data();
    if (cfg < 0) cfg = 0x47;
    cfg &= ~0x03;      // sin IRQ mientras configuramos
    cfg |= 0x40;       // traducción a set 1
    cfg &= ~0x30;      // relojes habilitados
    ctrl_cmd(0x60);
    write_data((uint8_t)cfg);

    ctrl_cmd(0xAE);
    ctrl_cmd(0xA8);
    flush();

    // Teclado: valores por defecto y activar exploración
    if (!dev_write(false, 0xF6)) klog("PS/2: el teclado no respondió a F6\n");
    dev_write(false, 0xF4);

    detect_pointer();
    if (ptr_type != Ptr::NONE && !mouse_write(0xF4)) klog("PS/2: no se pudo activar el ratón\n");
    flush();

    cfg |= 0x01 | (ptr_type != Ptr::NONE ? 0x02 : 0);
    ctrl_cmd(0x60);
    write_data((uint8_t)cfg);

    idt::set_irq_handler(1, kbd_irq);
    if (ptr_type != Ptr::NONE) idt::set_irq_handler(12, mouse_irq);
    klog("PS/2: teclado español listo\n");
}

void ps2::poll() {
    if (ptr_type != Ptr::SYNAPTICS) return;
    IrqGuard g;
    if (tap_pending && pit::ticks() - tap_time >= TAP_MS && !tp_touching) {
        tap_pending = false;
        // Toque simple: clic izquierdo
        emit_mouse(0, 0, 0, tp_phys_buttons | MB_LEFT);
        emit_mouse(0, 0, 0, tp_phys_buttons);
        tp_reported = tp_phys_buttons;
    }
}

bool ps2::has_touchpad() { return ptr_type == Ptr::SYNAPTICS; }

const char* ps2::pointer_name() {
    switch (ptr_type) {
    case Ptr::SYNAPTICS: return "Touchpad Synaptics PS/2";
    case Ptr::WHEEL: return "Ratón PS/2 con rueda";
    case Ptr::PS2: return "Ratón PS/2";
    default: return "Ninguno";
    }
}


#include "lib/log.h"
#include "lib/string.h"
#include "arch/io.h"
#include "gfx/bootscreen.h"

static constexpr uint16_t COM1 = 0x3F8;
static bool serial_ok = false;

static constexpr unsigned LOG_CAP = 32768;
static char log_mem[LOG_CAP];
static unsigned log_len = 0;

void log_init() {
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x01); // 115200 baudios
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x1E); // modo loopback para probar el chip
    outb(COM1 + 0, 0xAE);
    serial_ok = inb(COM1 + 0) == 0xAE;
    outb(COM1 + 4, 0x0F);
}

static void serial_put(char c) {
    if (!serial_ok) return;
    for (int i = 0; i < 100000 && !(inb(COM1 + 5) & 0x20); i++) {}
    outb(COM1, (uint8_t)c);
}

static void emit(const char* s) {
    for (; *s; s++) {
        if (*s == '\n') serial_put('\r');
        serial_put(*s);
        if (log_len < LOG_CAP - 1) log_mem[log_len++] = *s;
    }
    log_mem[log_len] = 0;
}

void klog(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    kvsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    IrqGuard g;
    emit(buf);
    bootscreen_log(buf);
}

void panic(const char* fmt, ...) {
    cli();
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    kvsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    emit("\n*** PANIC: ");
    emit(buf);
    emit("\n");
    bluescreen("Error fatal del sistema", buf);
    halt_forever();
}

const char* log_buffer() { return log_mem; }
unsigned log_length() { return log_len; }

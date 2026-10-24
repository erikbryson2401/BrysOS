// Reloj de tiempo real CMOS (MC146818)
#include "drivers/rtc.h"
#include "arch/io.h"

namespace {
uint8_t cmos(uint8_t reg) {
    outb(0x70, reg | 0x80); // con NMI deshabilitada
    return inb(0x71);
}
bool updating() { return cmos(0x0A) & 0x80; }

struct Raw { uint8_t s, m, h, d, mo, y, c; };
Raw read_raw() {
    while (updating()) {}
    Raw r{cmos(0x00), cmos(0x02), cmos(0x04), cmos(0x07), cmos(0x08), cmos(0x09), cmos(0x32)};
    return r;
}
int bcd(uint8_t v) { return (v & 0x0F) + (v >> 4) * 10; }
}

DateTime rtc::hardware_now() {
    IrqGuard g;
    Raw a, b;
    // lee dos veces hasta obtener valores coherentes
    a = read_raw();
    for (int i = 0; i < 5; i++) {
        b = read_raw();
        if (a.s == b.s && a.m == b.m && a.h == b.h && a.d == b.d && a.mo == b.mo && a.y == b.y) break;
        a = b;
    }
    uint8_t regb = cmos(0x0B);
    bool is_bcd = !(regb & 0x04);
    bool h12 = !(regb & 0x02);
    bool pm = b.h & 0x80;
    uint8_t hr = b.h & 0x7F;
    DateTime t;
    t.second = is_bcd ? bcd(b.s) : b.s;
    t.minute = is_bcd ? bcd(b.m) : b.m;
    t.hour = is_bcd ? bcd(hr) : hr;
    t.day = is_bcd ? bcd(b.d) : b.d;
    t.month = is_bcd ? bcd(b.mo) : b.mo;
    int yy = is_bcd ? bcd(b.y) : b.y;
    int cc = is_bcd ? bcd(b.c) : b.c;
    if (cc < 19 || cc > 21) cc = 20;
    t.year = cc * 100 + yy;
    if (h12) {
        if (pm && t.hour != 12) t.hour += 12;
        if (!pm && t.hour == 12) t.hour = 0;
    }
    return t;
}

namespace {
bool hw_utc = false;
int tz_offset = 0;
void cmos_write(uint8_t reg, uint8_t v) {
    outb(0x70, reg | 0x80);
    outb(0x71, v);
}
uint8_t to_bcd(int v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }
}

void rtc::configure(bool utc, int offset) {
    hw_utc = utc;
    tz_offset = offset;
}
bool rtc::hardware_is_utc() { return hw_utc; }
int rtc::utc_offset() { return tz_offset; }

int rtc::days_in_month(int y, int m) {
    static const int d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
    return (m >= 1 && m <= 12) ? d[m - 1] : 30;
}

DateTime rtc::add_minutes(DateTime t, int minutes) {
    int total = t.hour * 60 + t.minute + minutes;
    int day_shift = 0;
    while (total < 0) { total += 1440; day_shift--; }
    while (total >= 1440) { total -= 1440; day_shift++; }
    t.hour = total / 60;
    t.minute = total % 60;
    while (day_shift > 0) {
        if (++t.day > days_in_month(t.year, t.month)) { t.day = 1; if (++t.month > 12) { t.month = 1; t.year++; } }
        day_shift--;
    }
    while (day_shift < 0) {
        if (--t.day < 1) { if (--t.month < 1) { t.month = 12; t.year--; } t.day = days_in_month(t.year, t.month); }
        day_shift++;
    }
    return t;
}

DateTime rtc::now() {
    DateTime t = hardware_now();
    return hw_utc ? add_minutes(t, tz_offset) : t;
}

void rtc::set_local(const DateTime& local) {
    DateTime t = hw_utc ? add_minutes(local, -tz_offset) : local;
    IrqGuard g;
    uint8_t regb = cmos(0x0B);
    bool is_bcd = !(regb & 0x04);
    bool h12 = !(regb & 0x02);
    while (updating()) {}
    cmos_write(0x0B, regb | 0x80);   // detiene las actualizaciones mientras se escribe
    auto enc = [&](int v) { return is_bcd ? to_bcd(v) : (uint8_t)v; };
    int hour = t.hour;
    uint8_t hv;
    if (h12) {
        bool pm = hour >= 12;
        int h = hour % 12;
        if (h == 0) h = 12;
        hv = (uint8_t)(enc(h) | (pm ? 0x80 : 0));
    } else {
        hv = enc(hour);
    }
    cmos_write(0x00, enc(t.second));
    cmos_write(0x02, enc(t.minute));
    cmos_write(0x04, hv);
    cmos_write(0x07, enc(t.day));
    cmos_write(0x08, enc(t.month));
    cmos_write(0x09, enc(t.year % 100));
    cmos_write(0x32, enc(t.year / 100));
    cmos_write(0x0B, regb & ~0x80);
}

int DateTime::weekday() const {
    // Algoritmo de Sakamoto
    static const int tt[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    int y = year - (month < 3);
    return (y + y / 4 - y / 100 + y / 400 + tt[(month - 1) % 12] + day) % 7;
}

uint16_t rtc::fat_date(const DateTime& t) {
    int y = t.year < 1980 ? 0 : t.year - 1980;
    return (uint16_t)((y << 9) | (t.month << 5) | t.day);
}
uint16_t rtc::fat_time(const DateTime& t) {
    return (uint16_t)((t.hour << 11) | (t.minute << 5) | (t.second / 2));
}
DateTime rtc::from_fat(uint16_t d, uint16_t tm) {
    DateTime t;
    t.year = 1980 + (d >> 9);
    t.month = (d >> 5) & 15;
    t.day = d & 31;
    t.hour = tm >> 11;
    t.minute = (tm >> 5) & 63;
    t.second = (tm & 31) * 2;
    return t;
}

const char* weekday_name(int d) {
    static const char* n[] = {"domingo", "lunes", "martes", "miércoles", "jueves", "viernes", "sábado"};
    return n[d % 7];
}
const char* month_name(int m) {
    static const char* n[] = {"enero", "febrero", "marzo", "abril", "mayo", "junio", "julio",
                              "agosto", "septiembre", "octubre", "noviembre", "diciembre"};
    return (m >= 1 && m <= 12) ? n[m - 1] : "?";
}

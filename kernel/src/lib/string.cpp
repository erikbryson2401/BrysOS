#include "lib/string.h"

extern "C" {

void* memcpy(void* dst, const void* src, size_t n) {
    void* ret = dst;
    asm volatile("rep movsb" : "+D"(dst), "+S"(src), "+c"(n)::"memory");
    return ret;
}

void* memset(void* dst, int c, size_t n) {
    void* ret = dst;
    asm volatile("rep stosb" : "+D"(dst), "+c"(n) : "a"(c) : "memory");
    return ret;
}

void* memmove(void* dst, const void* src, size_t n) {
    auto* d = (uint8_t*)dst;
    auto* s = (const uint8_t*)src;
    if (d == s || n == 0) return dst;
    if (d < s || d >= s + n) return memcpy(dst, src, n);
    for (size_t i = n; i > 0; i--) d[i - 1] = s[i - 1];
    return dst;
}

int memcmp(const void* a, const void* b, size_t n) {
    auto* x = (const uint8_t*)a;
    auto* y = (const uint8_t*)b;
    for (size_t i = 0; i < n; i++)
        if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
    return 0;
}

size_t strlen(const char* s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

int strcmp(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return (uint8_t)*a - (uint8_t)*b;
}

int strncmp(const char* a, const char* b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i] || !a[i]) return (uint8_t)a[i] - (uint8_t)b[i];
    }
    return 0;
}

char* strchr(const char* s, int c) {
    for (;; s++) {
        if (*s == (char)c) return (char*)s;
        if (!*s) return nullptr;
    }
}

char* strstr(const char* hay, const char* needle) {
    size_t n = strlen(needle);
    for (; *hay; hay++)
        if (!strncmp(hay, needle, n)) return (char*)hay;
    return n ? nullptr : (char*)hay;
}

char* strcpy(char* d, const char* s) {
    char* r = d;
    while ((*d++ = *s++)) {}
    return r;
}

// gcc puede emitir llamadas a esta función
void __cxa_pure_virtual() { for (;;) asm volatile("cli; hlt"); }
// Los destructores globales nunca se ejecutan en un kernel
int atexit(void (*)(void)) { return 0; }
void* __dso_handle = nullptr;

}

void strlcpy(char* d, const char* s, size_t cap) {
    if (!cap) return;
    size_t i = 0;
    for (; i + 1 < cap && s[i]; i++) d[i] = s[i];
    d[i] = 0;
}

void strlcat(char* d, const char* s, size_t cap) {
    size_t l = strlen(d);
    if (l + 1 >= cap) return;
    strlcpy(d + l, s, cap - l);
}

char to_upper(char c) {
    uint8_t u = (uint8_t)c;
    if (u >= 'a' && u <= 'z') return (char)(u - 32);
    if (u >= 0xE0 && u <= 0xFE && u != 0xF7) return (char)(u - 32); // Latin-1
    return c;
}

char to_lower(char c) {
    uint8_t u = (uint8_t)c;
    if (u >= 'A' && u <= 'Z') return (char)(u + 32);
    if (u >= 0xC0 && u <= 0xDE && u != 0xD7) return (char)(u + 32);
    return c;
}

int stricmp(const char* a, const char* b) {
    while (*a && to_upper(*a) == to_upper(*b)) { a++; b++; }
    return (uint8_t)to_upper(*a) - (uint8_t)to_upper(*b);
}

bool str_icontains(const char* hay, const char* needle) {
    size_t n = strlen(needle);
    if (!n) return true;
    for (; *hay; hay++) {
        size_t i = 0;
        while (i < n && hay[i] && to_upper(hay[i]) == to_upper(needle[i])) i++;
        if (i == n) return true;
    }
    return false;
}

bool wildcard_match(const char* p, const char* s) {
    const char* star = nullptr;
    const char* ss = nullptr;
    while (*s) {
        if (*p == '?' || (*p && *p != '*' && to_upper(*p) == to_upper(*s))) { p++; s++; }
        else if (*p == '*') { star = p++; ss = s; }
        else if (star) { p = star + 1; s = ++ss; }
        else return false;
    }
    while (*p == '*') p++;
    return *p == 0;
}

// --------------------------------------------------------------------------
// printf mínimo
// --------------------------------------------------------------------------
namespace {
struct Out {
    char* buf;
    size_t cap;
    size_t len;
    void put(char c) {
        if (len + 1 < cap) buf[len] = c;
        len++;
    }
};

void put_num(Out& o, uint64_t v, int base, bool upper, int width, bool zero, bool neg, bool left) {
    char tmp[24];
    int n = 0;
    const char* digs = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    do { tmp[n++] = digs[v % base]; v /= base; } while (v);
    int total = n + (neg ? 1 : 0);
    if (!left && !zero) for (int i = total; i < width; i++) o.put(' ');
    if (neg) o.put('-');
    if (!left && zero) for (int i = total; i < width; i++) o.put('0');
    while (n) o.put(tmp[--n]);
    if (left) for (int i = total; i < width; i++) o.put(' ');
}
}

int kvsnprintf(char* buf, size_t cap, const char* fmt, va_list ap) {
    Out o{buf, cap, 0};
    for (; *fmt; fmt++) {
        if (*fmt != '%') { o.put(*fmt); continue; }
        fmt++;
        bool left = false, zero = false;
        while (*fmt == '-' || *fmt == '0') {
            if (*fmt == '-') left = true; else zero = true;
            fmt++;
        }
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');
        int lng = 0;
        while (*fmt == 'l') { lng++; fmt++; }
        if (*fmt == 'z') { lng = 2; fmt++; }
        switch (*fmt) {
        case 'd': case 'i': {
            int64_t v = lng ? va_arg(ap, int64_t) : va_arg(ap, int);
            bool neg = v < 0;
            put_num(o, neg ? (uint64_t)(-v) : (uint64_t)v, 10, false, width, zero, neg, left);
            break;
        }
        case 'u': {
            uint64_t v = lng ? va_arg(ap, uint64_t) : va_arg(ap, unsigned);
            put_num(o, v, 10, false, width, zero, false, left);
            break;
        }
        case 'x': case 'X': {
            uint64_t v = lng ? va_arg(ap, uint64_t) : va_arg(ap, unsigned);
            put_num(o, v, 16, *fmt == 'X', width, zero, false, left);
            break;
        }
        case 'p':
            o.put('0'); o.put('x');
            put_num(o, (uint64_t)va_arg(ap, void*), 16, false, 16, true, false, false);
            break;
        case 'c': o.put((char)va_arg(ap, int)); break;
        case 's': {
            const char* s = va_arg(ap, const char*);
            if (!s) s = "(null)";
            int l = (int)strlen(s);
            if (!left) for (int i = l; i < width; i++) o.put(' ');
            while (*s) o.put(*s++);
            if (left) for (int i = l; i < width; i++) o.put(' ');
            break;
        }
        case '%': o.put('%'); break;
        default: o.put('%'); o.put(*fmt); break;
        }
    }
    if (cap) buf[o.len < cap ? o.len : cap - 1] = 0;
    return (int)o.len;
}

int ksnprintf(char* buf, size_t cap, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = kvsnprintf(buf, cap, fmt, ap);
    va_end(ap);
    return r;
}

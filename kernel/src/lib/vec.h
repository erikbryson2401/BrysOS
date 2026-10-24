#pragma once
#include <stddef.h>
#include "mm/memory.h"
#include "lib/string.h"

template <typename T> static inline T&& move_val(T& v) { return static_cast<T&&>(v); }

// Vector dinámico mínimo
template <typename T>
class Vec {
    T* d = nullptr;
    size_t n = 0, cap = 0;

public:
    Vec() = default;
    Vec(const Vec& o) { *this = o; }
    Vec(Vec&& o) : d(o.d), n(o.n), cap(o.cap) { o.d = nullptr; o.n = o.cap = 0; }
    ~Vec() { clear(); mm::kfree(d); }
    Vec& operator=(const Vec& o) {
        if (this == &o) return *this;
        clear();
        reserve(o.n);
        for (size_t i = 0; i < o.n; i++) new (&d[i]) T(o.d[i]);
        n = o.n;
        return *this;
    }
    Vec& operator=(Vec&& o) {
        if (this == &o) return *this;
        clear(); mm::kfree(d);
        d = o.d; n = o.n; cap = o.cap;
        o.d = nullptr; o.n = o.cap = 0;
        return *this;
    }
    void reserve(size_t c) {
        if (c <= cap) return;
        T* nd = (T*)mm::kmalloc(c * sizeof(T));
        for (size_t i = 0; i < n; i++) { new (&nd[i]) T(move_val(d[i])); d[i].~T(); }
        mm::kfree(d);
        d = nd;
        cap = c;
    }
    void push(const T& v) {
        if (n == cap) reserve(cap ? cap * 2 : 8);
        new (&d[n++]) T(v);
    }
    void push(T&& v) {
        if (n == cap) reserve(cap ? cap * 2 : 8);
        new (&d[n++]) T(move_val(v));
    }
    void insert(size_t i, const T& v) {
        if (i >= n) { push(v); return; }
        T last = d[n - 1];
        push(move_val(last));
        for (size_t k = n - 2; k > i; k--) d[k] = d[k - 1];
        d[i] = v;
    }
    void erase(size_t i) {
        if (i >= n) return;
        for (size_t k = i; k + 1 < n; k++) d[k] = move_val(d[k + 1]);
        d[--n].~T();
    }
    bool remove(const T& v) {
        for (size_t i = 0; i < n; i++)
            if (d[i] == v) { erase(i); return true; }
        return false;
    }
    int index_of(const T& v) const {
        for (size_t i = 0; i < n; i++)
            if (d[i] == v) return (int)i;
        return -1;
    }
    void pop() { if (n) d[--n].~T(); }
    void clear() { for (size_t i = 0; i < n; i++) d[i].~T(); n = 0; }
    void resize(size_t c) {
        reserve(c);
        while (n < c) new (&d[n++]) T();
        while (n > c) d[--n].~T();
    }
    size_t size() const { return n; }
    bool empty() const { return n == 0; }
    T& operator[](size_t i) { return d[i]; }
    const T& operator[](size_t i) const { return d[i]; }
    T& back() { return d[n - 1]; }
    T* data() { return d; }
    const T* data() const { return d; }
    T* begin() { return d; }
    T* end() { return d + n; }
    const T* begin() const { return d; }
    const T* end() const { return d + n; }

    // Ordenación por inserción (estable); cmp(a,b) < 0 si a va antes
    template <typename F> void sort(F cmp) {
        for (size_t i = 1; i < n; i++) {
            T v = move_val(d[i]);
            size_t j = i;
            while (j > 0 && cmp(v, d[j - 1]) < 0) { d[j] = move_val(d[j - 1]); j--; }
            d[j] = move_val(v);
        }
    }
};

// Cadena dinámica (Latin-1)
class String {
    char* d = nullptr;
    size_t n = 0, cap = 0;
    void grow(size_t need) {
        if (need + 1 <= cap) return;
        size_t c = cap ? cap * 2 : 16;
        while (c < need + 1) c *= 2;
        char* nd = (char*)mm::kmalloc(c);
        if (d) memcpy(nd, d, n);
        nd[n] = 0;
        mm::kfree(d);
        d = nd;
        cap = c;
    }

public:
    String() = default;
    String(const char* s) { append(s); }
    String(const char* s, size_t len) { append(s, len); }
    String(const String& o) { append(o.c_str(), o.n); }
    String(String&& o) : d(o.d), n(o.n), cap(o.cap) { o.d = nullptr; o.n = o.cap = 0; }
    ~String() { mm::kfree(d); }
    String& operator=(const String& o) { if (this != &o) { clear(); append(o.c_str(), o.n); } return *this; }
    String& operator=(String&& o) {
        if (this != &o) { mm::kfree(d); d = o.d; n = o.n; cap = o.cap; o.d = nullptr; o.n = o.cap = 0; }
        return *this;
    }
    String& operator=(const char* s) {
        if (d && s >= d && s < d + cap) {   // el texto es parte de sí misma: se copia antes
            String t(s);
            return *this = move_val(t);
        }
        clear();
        append(s);
        return *this;
    }

    static String format(const char* fmt, ...) __attribute__((format(printf, 1, 2))) {
        char buf[1024];
        va_list ap;
        va_start(ap, fmt);
        kvsnprintf(buf, sizeof buf, fmt, ap);
        va_end(ap);
        return String(buf);
    }

    const char* c_str() const { return d ? d : ""; }
    size_t size() const { return n; }
    bool empty() const { return n == 0; }
    const char& operator[](size_t i) const { return d[i]; }
    char& operator[](size_t i) { return d[i]; }
    void clear() { n = 0; if (d) d[0] = 0; }
    void append(const char* s, size_t len) {
        grow(n + len);
        memcpy(d + n, s, len);
        n += len;
        d[n] = 0;
    }
    void append(const char* s) { append(s, strlen(s)); }
    void append(char c) { append(&c, 1); }
    String& operator+=(const char* s) { append(s); return *this; }
    String& operator+=(const String& s) { append(s.c_str(), s.n); return *this; }
    String& operator+=(char c) { append(c); return *this; }
    String operator+(const char* s) const { String r(*this); r += s; return r; }
    String operator+(const String& s) const { String r(*this); r += s; return r; }
    bool operator==(const char* s) const { return strcmp(c_str(), s) == 0; }
    bool operator==(const String& s) const { return strcmp(c_str(), s.c_str()) == 0; }
    bool operator!=(const char* s) const { return !(*this == s); }

    void insert(size_t pos, const char* s, size_t len) {
        if (pos > n) pos = n;
        grow(n + len);
        memmove(d + pos + len, d + pos, n - pos);
        memcpy(d + pos, s, len);
        n += len;
        d[n] = 0;
    }
    void erase(size_t pos, size_t len) {
        if (pos >= n) return;
        if (pos + len > n) len = n - pos;
        memmove(d + pos, d + pos + len, n - pos - len);
        n -= len;
        d[n] = 0;
    }
    String substr(size_t pos, size_t len = (size_t)-1) const {
        if (pos >= n) return String();
        if (len > n - pos) len = n - pos;
        return String(d + pos, len);
    }
    int find(char c, size_t from = 0) const {
        for (size_t i = from; i < n; i++) if (d[i] == c) return (int)i;
        return -1;
    }
    int rfind(char c) const {
        for (size_t i = n; i > 0; i--) if (d[i - 1] == c) return (int)(i - 1);
        return -1;
    }
};

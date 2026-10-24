// Calculadora estándar con aritmética decimal propia (18 dígitos significativos,
// sin coma flotante del procesador)
#include "apps/common.h"

namespace {
constexpr uint64_t P18 = 1000000000000000000ull; // 10^18
constexpr uint64_t P17 = 100000000000000000ull;

// División de 128 bits entre 64 sin recurrir a libgcc: (hi:lo) / d
struct U128 { uint64_t hi, lo; };
inline U128 mul64(uint64_t a, uint64_t b) {
    uint64_t lo, hi;
    asm("mulq %3" : "=a"(lo), "=d"(hi) : "a"(a), "rm"(b));
    return {hi, lo};
}
inline U128 divmod(U128 n, uint64_t d, uint64_t& rem) {
    uint64_t qh = n.hi / d, r = n.hi % d, ql;
    asm("divq %4" : "=a"(ql), "=d"(rem) : "a"(n.lo), "d"(r), "rm"(d));
    return {qh, ql};
}
inline bool ge(U128 a, uint64_t b) { return a.hi || a.lo >= b; }

struct Dec {
    bool neg = false;
    uint64_t m = 0;   // mantisa
    int e = 0;        // exponente decimal: valor = m * 10^e
    bool err = false;

    static Dec from_int(int64_t v) { Dec d; d.neg = v < 0; d.m = (uint64_t)(v < 0 ? -v : v); d.norm(); return d; }

    void norm() {
        if (m == 0) { neg = false; e = 0; return; }
        while (m >= P18) { uint64_t r = m % 10; m /= 10; if (r >= 5) m++; e++; }
        while (m % 10 == 0) { m /= 10; e++; }
        if (e > 400) err = true;
        if (e < -400) { m = 0; e = 0; neg = false; }
    }
    bool zero() const { return m == 0; }
};

Dec neg(Dec a) { if (!a.zero()) a.neg = !a.neg; return a; }

Dec dadd(Dec a, Dec b) {
    if (a.err || b.err) { Dec r; r.err = true; return r; }
    if (a.zero()) return b;
    if (b.zero()) return a;
    // alinea exponentes: baja el exponente del mayor multiplicando su mantisa
    if (a.e < b.e) { Dec t = a; a = b; b = t; }
    while (a.e > b.e && a.m < P17) { a.m *= 10; a.e--; }
    while (a.e > b.e) {
        if (b.m == 0) break;
        uint64_t r = b.m % 10; b.m /= 10; if (r >= 5) b.m++; b.e++;
    }
    if (b.m == 0) return a;
    Dec r;
    r.e = a.e;
    if (a.neg == b.neg) {
        uint64_t sm = a.m + b.m; // < 2*10^18: cabe en 64 bits
        while (sm >= P18) { sm /= 10; r.e++; }
        r.m = sm;
        r.neg = a.neg;
    } else if (a.m >= b.m) {
        r.m = a.m - b.m;
        r.neg = a.neg;
    } else {
        r.m = b.m - a.m;
        r.neg = b.neg;
    }
    r.norm();
    return r;
}

Dec dsub(Dec a, Dec b) { return dadd(a, neg(b)); }

Dec dmul(Dec a, Dec b) {
    Dec r;
    if (a.err || b.err) { r.err = true; return r; }
    U128 p = mul64(a.m, b.m);
    r.e = a.e + b.e;
    while (ge(p, P18)) {
        uint64_t rem;
        p = divmod(p, 10, rem);
        if (rem >= 5) { p.lo++; if (!p.lo) p.hi++; }
        r.e++;
    }
    r.m = p.lo;
    r.neg = a.neg != b.neg;
    r.norm();
    return r;
}

Dec ddiv(Dec a, Dec b) {
    Dec r;
    if (a.err || b.err || b.zero()) { r.err = true; return r; }
    if (a.zero()) return r;
    uint64_t rem = a.m % b.m;
    uint64_t q = a.m / b.m;
    int e = a.e - b.e;
    while (q < P17 && rem) {
        U128 n = mul64(rem, 10);   // rem < b.m, así que n / b.m < 10
        uint64_t r2;
        U128 qd = divmod(n, b.m, r2);
        q = q * 10 + qd.lo;
        rem = r2;
        e--;
    }
    if (rem >= b.m - rem && q + 1 < P18) q++;
    r.m = q;
    r.e = e;
    r.neg = a.neg != b.neg;
    r.norm();
    return r;
}

int cmp_abs(const Dec& a, const Dec& b) {
    Dec d = dsub(Dec{false, a.m, a.e}, Dec{false, b.m, b.e});
    return d.zero() ? 0 : (d.neg ? -1 : 1);
}

Dec dsqrt(Dec a) {
    Dec r;
    if (a.err || a.neg) { r.err = true; return r; }
    if (a.zero()) return r;
    // estimación inicial: 10^(dígitos/2)
    int digits = 0;
    for (uint64_t t = a.m; t; t /= 10) digits++;
    int mag = digits + a.e;
    Dec x;
    x.m = 1;
    x.e = mag / 2;
    x.norm();
    Dec half;
    half.m = 5;
    half.e = -1;
    for (int i = 0; i < 80; i++) {
        Dec nx = dmul(dadd(x, ddiv(a, x)), half);
        if (!cmp_abs(nx, x)) break;
        x = nx;
    }
    return x;
}

// Conversión a texto con coma decimal
String to_string(const Dec& d) {
    if (d.err) return T("No se puede dividir por cero.");
    if (d.zero()) return "0,";
    char digs[24];
    int n = 0;
    for (uint64_t t = d.m; t; t /= 10) digs[n++] = (char)('0' + t % 10);
    // digs está al revés; n dígitos
    int point = n + d.e; // posición de la coma contando desde la izquierda
    String s;
    if (d.neg) s += "-";
    if (point > 16 || point < -14) {
        // notación científica
        s.append(digs[n - 1]);
        s += ",";
        for (int i = n - 2; i >= 0 && i >= n - 16; i--) s.append(digs[i]);
        s += String::format(T("e%c%d"), point - 1 >= 0 ? '+' : '-', abs_val(point - 1));
        return s;
    }
    if (point <= 0) {
        s += "0,";
        for (int i = 0; i < -point; i++) s += "0";
        for (int i = n - 1; i >= 0 && (int)s.size() < 20; i--) s.append(digs[i]);
        return s;
    }
    for (int i = 0; i < point; i++) s.append(i < n ? digs[n - 1 - i] : '0');
    s += ",";
    for (int i = point; i < n; i++) s.append(digs[n - 1 - i]);
    return s;
}

Dec parse(const String& s) {
    Dec d;
    bool frac = false;
    int fd = 0;
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (c == '-') d.neg = true;
        else if (c == ',') frac = true;
        else if (c >= '0' && c <= '9') {
            if (d.m < P17) { d.m = d.m * 10 + (uint64_t)(c - '0'); if (frac) fd++; }
            else if (!frac) d.e++;
        }
    }
    d.e -= fd;
    d.norm();
    return d;
}

enum {
    B_0 = 1, B_1, B_2, B_3, B_4, B_5, B_6, B_7, B_8, B_9, B_DOT, B_SIGN, B_ADD, B_SUB, B_MUL, B_DIV, B_EQ,
    B_SQRT, B_PCT, B_INV, B_BACK, B_CE, B_C, B_MC, B_MR, B_MS, B_MP, M_COPY = 100, M_PASTE, M_ABOUT,
};

class Calc : public Window {
public:
    String entry = "0";
    bool typing = false;    // el usuario está escribiendo un número
    Dec acc;
    int op = 0;
    bool have_acc = false;
    Dec memory;
    bool has_mem = false;
    Dec last_operand;
    int last_op = 0;
    bool error = false;
    Label* mem_label;

    Calc() : Window(T("Calculadora"), 252, 196, WF_FIXED) {
        icon = &icon_calc;
        auto* mb = new MenuBar();
        mb->add(T("&Edición"))->add(T("&Copiar"), M_COPY, T("Ctrl+C")).add(T("&Pegar"), M_PASTE, T("Ctrl+V"));
        mb->add(T("Ay&uda"))->add(T("&Acerca de la Calculadora"), M_ABOUT);
        attach_menubar(this, mb);
        mem_label = add(new Label({10, 44, 24, 22}, "", 1));
        mem_label->sunken = true;
        // Fila superior
        btn({44, 44, 62, 24}, T("Retroceso"), B_BACK, 0x800000);
        btn({110, 44, 62, 24}, "CE", B_CE, 0x800000);
        btn({176, 44, 66, 24}, "C", B_C, 0x800000);
        const char* mem[] = {"MC", "MR", "MS", "M+"};
        for (int i = 0; i < 4; i++) btn({10, 74 + i * 30, 30, 26}, mem[i], B_MC + i, 0xC00000);
        struct K { const char* t; int id; uint32_t c; };
        const K keys[4][5] = {
            {{"7", B_7, 0x0000C0}, {"8", B_8, 0x0000C0}, {"9", B_9, 0x0000C0}, {"/", B_DIV, 0xC00000}, {"raíz", B_SQRT, 0x000080}},
            {{"4", B_4, 0x0000C0}, {"5", B_5, 0x0000C0}, {"6", B_6, 0x0000C0}, {"*", B_MUL, 0xC00000}, {"%", B_PCT, 0x000080}},
            {{"1", B_1, 0x0000C0}, {"2", B_2, 0x0000C0}, {"3", B_3, 0x0000C0}, {"-", B_SUB, 0xC00000}, {"1/x", B_INV, 0x000080}},
            {{"0", B_0, 0x0000C0}, {"+/-", B_SIGN, 0x0000C0}, {",", B_DOT, 0x0000C0}, {"+", B_ADD, 0xC00000}, {"=", B_EQ, 0xC00000}},
        };
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 5; c++) btn({50 + c * 39, 74 + r * 30, 35, 26}, keys[r][c].t, keys[r][c].id, keys[r][c].c);
    }

    // Botón con texto de color (como la calculadora clásica)
    struct ColorButton : public Button {
        uint32_t color;
        ColorButton(const Rect& r, const char* t, int id, uint32_t c) : Button(r, t, id), color(c) { focusable = false; }
        void paint(Surface& s) override {
            Rect a{0, 0, r.w, r.h};
            bool sunk = pressing && inside;
            s.fill(a, col::FACE);
            if (sunk) s.bevel_pressed(a); else s.bevel_raised(a);
            int o = sunk ? 1 : 0;
            s.text_centered(font_ui, a.offset(o, o), text.c_str(), color);
        }
    };

    void btn(const Rect& r, const char* t, int id, uint32_t c) { add(new ColorButton(r, T(t), id, c)); }

    void on_paint(Surface& s) override {
        s.clear(col::FACE);
        Rect d{10, 8, client_w() - 20, 26};
        s.fill(d, col::WHITE);
        s.bevel_sunken(d);
        const char* t = entry.c_str();
        int tw = font_ui.text_width(t);
        s.text(font_ui, d.right() - 6 - tw, d.y + (d.h - font_ui.height) / 2, t, col::BLACK);
    }

    Dec current() { return parse(entry); }
    void show(const Dec& d) {
        entry = to_string(d);
        error = d.err;
        invalidate();
    }

    Dec apply(int o, Dec a, Dec b) {
        switch (o) {
        case B_ADD: return dadd(a, b);
        case B_SUB: return dsub(a, b);
        case B_MUL: return dmul(a, b);
        case B_DIV: return ddiv(a, b);
        }
        return b;
    }

    void digit(char c) {
        if (error) clear_all();
        if (!typing) { entry = "0"; typing = true; }
        int digits = 0;
        for (size_t i = 0; i < entry.size(); i++) if (entry[i] >= '0' && entry[i] <= '9') digits++;
        if (digits >= 16) return;
        if (entry == "0" || entry == "-0") {
            bool n = entry[0] == '-';
            entry = n ? "-" : "";
            entry.append(c);
        } else {
            entry.append(c);
        }
        invalidate();
    }

    void clear_all() {
        entry = "0";
        typing = false;
        have_acc = false;
        op = 0;
        last_op = 0;
        error = false;
        invalidate();
    }

    void press(int id) {
        if (error && id != B_C && id != B_CE) return;
        if (id >= B_0 && id <= B_9) { digit((char)('0' + id - B_0)); return; }
        switch (id) {
        case B_DOT:
            if (!typing) { entry = "0"; typing = true; }
            if (entry.find(',') < 0) entry += ",";
            invalidate();
            return;
        case B_SIGN:
            if (typing) {
                if (entry[0] == '-') entry.erase(0, 1);
                else if (entry != "0") entry.insert(0, "-", 1);
                invalidate();
            } else {
                show(neg(current()));
            }
            return;
        case B_BACK:
            if (!typing) return;
            entry.erase(entry.size() - 1, 1);
            if (entry.empty() || entry == "-") entry = "0";
            invalidate();
            return;
        case B_CE: entry = "0"; typing = false; error = false; invalidate(); return;
        case B_C: clear_all(); return;
        case B_ADD: case B_SUB: case B_MUL: case B_DIV: {
            Dec v = current();
            if (have_acc && op && typing) { acc = apply(op, acc, v); show(acc); }
            else if (!have_acc || !op || typing) acc = v;
            have_acc = true;
            op = id;
            typing = false;
            last_op = 0;
            return;
        }
        case B_EQ: {
            Dec v = current();
            if (op) {
                last_operand = typing || !last_op ? v : last_operand;
                if (!typing && last_op == 0) last_operand = v;
                acc = apply(op, acc, last_operand);
                last_op = op;
                op = 0;
            } else if (last_op) {
                acc = apply(last_op, v, last_operand);
            } else {
                acc = v;
            }
            show(acc);
            have_acc = true;
            typing = false;
            return;
        }
        case B_SQRT: show(dsqrt(current())); typing = false; return;
        case B_INV: show(ddiv(Dec::from_int(1), current())); typing = false; return;
        case B_PCT: {
            Dec v = current();
            Dec p = have_acc ? dmul(acc, ddiv(v, Dec::from_int(100))) : ddiv(v, Dec::from_int(100));
            show(p);
            typing = true;
            return;
        }
        case B_MC: has_mem = false; memory = Dec(); mem_label->set_text(""); return;
        case B_MR: if (has_mem) { show(memory); typing = false; } return;
        case B_MS: memory = current(); has_mem = !memory.zero(); mem_label->set_text(has_mem ? "M" : ""); typing = false; return;
        case B_MP: memory = dadd(memory, current()); has_mem = !memory.zero(); mem_label->set_text(has_mem ? "M" : ""); typing = false; return;
        }
    }

    void on_command(int id, int) override {
        if (id == M_COPY) { wm::clipboard() = entry; return; }
        if (id == M_PASTE) {
            String& c = wm::clipboard();
            for (size_t i = 0; i < c.size(); i++) {
                char ch = c[i];
                if (ch >= '0' && ch <= '9') digit(ch);
                else if (ch == ',' || ch == '.') press(B_DOT);
            }
            return;
        }
        if (id == M_ABOUT) {
            msgbox(this, T("Acerca de la Calculadora"), T("Calculadora de BrysOS\nVersión 1.0\n\nAritmética decimal de 18 dígitos."), MB_OK, &icon_calc);
            return;
        }
        press(id);
    }

    bool on_key(const InputEvent& e) override {
        if (!e.pressed) return false;
        if (e.mods & MOD_CTRL) {
            if (e.key == 'c') { on_command(M_COPY, 0); return true; }
            if (e.key == 'v') { on_command(M_PASTE, 0); return true; }
            return false;
        }
        switch (e.key) {
        case KEY_ENTER: press(B_EQ); return true;
        case KEY_BACKSPACE: press(B_BACK); return true;
        case KEY_ESC: press(B_C); return true;
        case KEY_DELETE: press(B_CE); return true;
        case KEY_F9: press(B_SIGN); return true;
        }
        switch (e.ch) {
        case '+': press(B_ADD); return true;
        case '-': press(B_SUB); return true;
        case '*': press(B_MUL); return true;
        case '/': press(B_DIV); return true;
        case '=': press(B_EQ); return true;
        case '%': press(B_PCT); return true;
        case ',': case '.': press(B_DOT); return true;
        case '@': press(B_SQRT); return true;
        }
        if (e.ch >= '0' && e.ch <= '9') { digit((char)e.ch); return true; }
        return false;
    }
};
}

namespace apps {
void calc() {
    wm::add(new Calc());
}
}

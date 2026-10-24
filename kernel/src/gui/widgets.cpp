#include "gui/widgets.h"
#include "gui/menu.h"
#include "arch/idt.h"

// ---------------------------------------------------------------------------
// Utilidades
// ---------------------------------------------------------------------------
void draw_checker(Surface& s, const Rect& r) {
    for (int y = r.y; y < r.bottom(); y++)
        for (int x = r.x; x < r.right(); x++) s.put(x, y, ((x + y) & 1) ? col::HILIGHT : col::FACE);
}

void draw_arrow(Surface& s, int cx, int cy, int dir, uint32_t c) {
    for (int i = 0; i < 4; i++) {
        switch (dir) {
        case 0: s.hline(cx - i, cy - 2 + i, 2 * i + 1, c); break;
        case 1: s.hline(cx - 3 + i, cy - 1 + i, 7 - 2 * i, c); break;
        case 2: s.vline(cx - 2 + i, cy - i, 2 * i + 1, c); break;
        case 3: s.vline(cx - 1 + i, cy - 3 + i, 7 - 2 * i, c); break;
        }
    }
}

void draw_icon(Surface& s, const IconImg& ic, int x, int y) {
    for (int j = 0; j < ic.h; j++)
        for (int i = 0; i < ic.w; i++) {
            uint32_t c = ic.px[j * ic.w + i];
            if (c != col::TRANSPARENT) s.put(x + i, y + j, c);
        }
}

void draw_icon_selected(Surface& s, const IconImg& ic, int x, int y) {
    for (int j = 0; j < ic.h; j++)
        for (int i = 0; i < ic.w; i++) {
            uint32_t c = ic.px[j * ic.w + i];
            if (c == col::TRANSPARENT) continue;
            // trama 50% con azul marino, como Win98
            s.put(x + i, y + j, ((i + j) & 1) ? col::SELECT : c);
        }
}

// Parte una etiqueta en un máximo de 'maxl' líneas que quepan en 'width'
static int wrap_label(const Font& f, const char* t, int width, int starts[], int lens[], int maxl) {
    int n = 0, len = (int)strlen(t), pos = 0;
    while (pos < len && n < maxl) {
        int end = pos, last_space = -1;
        while (end < len && f.text_width(t + pos, end - pos + 1) <= width) {
            if (t[end] == ' ') last_space = end;
            end++;
        }
        if (end < len && last_space > pos && n < maxl - 1) end = last_space + 1;
        if (end == pos) end = pos + 1;
        starts[n] = pos;
        lens[n] = end - pos;
        n++;
        pos = end;
    }
    if (pos < len && n > 0) lens[n - 1] = -1; // marca de truncado
    return n;
}

void draw_icon_label(Surface& s, const Rect& cell, const IconPair* icon, const char* text, bool selected,
                     bool focused, bool white_text, uint32_t bg) {
    const Font& f = font_ui;
    int ix = cell.x + (cell.w - 32) / 2, iy = cell.y + 2;
    if (icon) {
        if (selected) draw_icon_selected(s, icon->big, ix, iy);
        else draw_icon(s, icon->big, ix, iy);
    }
    int starts[3], lens[3];
    int n = wrap_label(f, text, cell.w - 6, starts, lens, selected ? 3 : 2);
    int ty = iy + 34;
    int maxw = 0;
    char buf[160];
    for (int i = 0; i < n; i++) {
        int l = lens[i] < 0 ? (int)strlen(text + starts[i]) : lens[i];
        int w = f.text_width(text + starts[i], l);
        if (lens[i] < 0) w = min(cell.w - 6, w);
        maxw = max(maxw, w);
    }
    Rect tr{cell.x + (cell.w - maxw) / 2 - 2, ty - 1, maxw + 4, n * f.height + 2};
    if (selected) s.fill(tr, col::SELECT);
    else if (bg != 0xFFFFFFFF) s.fill(tr, bg);
    for (int i = 0; i < n; i++) {
        int l = lens[i];
        const char* p = text + starts[i];
        if (l < 0) {
            // última línea truncada con "..."
            int dots = f.text_width("...");
            l = (int)strlen(p);
            while (l > 0 && f.text_width(p, l) + dots > cell.w - 6) l--;
            strlcpy(buf, p, (size_t)l + 1);
            strlcat(buf, "...", sizeof buf);
            p = buf;
            l = (int)strlen(buf);
        }
        int w = f.text_width(p, l);
        uint32_t c = selected ? col::WHITE : (white_text ? col::WHITE : col::BLACK);
        s.text(f, cell.x + (cell.w - w) / 2, ty + i * f.height, p, c, l);
    }
    if (focused && selected) s.dotted_rect(tr, col::YELLOW);
}

// ---------------------------------------------------------------------------
// ScrollBar
// ---------------------------------------------------------------------------
Rect ScrollBar::thumb() const {
    if (!needed()) return {};
    int len = vertical ? r.h - 32 : r.w - 32;
    if (len < 8) return {};
    int tl = max(8, (int)((int64_t)len * page / total));
    int tp = max_pos() ? (int)((int64_t)(len - tl) * pos / max_pos()) : 0;
    return vertical ? Rect{r.x, r.y + 16 + tp, r.w, tl} : Rect{r.x + 16 + tp, r.y, tl, r.h};
}

void ScrollBar::paint(Surface& s) const {
    Rect a1 = vertical ? Rect{r.x, r.y, r.w, 16} : Rect{r.x, r.y, 16, r.h};
    Rect a2 = vertical ? Rect{r.x, r.bottom() - 16, r.w, 16} : Rect{r.right() - 16, r.y, 16, r.h};
    Rect track = vertical ? Rect{r.x, r.y + 16, r.w, r.h - 32} : Rect{r.x + 16, r.y, r.w - 32, r.h};
    draw_checker(s, track);
    bool en = needed();
    for (int k = 0; k < 2; k++) {
        Rect a = k ? a2 : a1;
        s.fill(a, col::FACE);
        if (pressed == k + 1) s.rect(a, col::SHADOW); else s.bevel_raised(a);
        int o = pressed == k + 1 ? 1 : 0;
        int dir = vertical ? (k ? 1 : 0) : (k ? 3 : 2);
        if (!en) draw_arrow(s, a.x + a.w / 2 + 1, a.y + a.h / 2 + 1, dir, col::HILIGHT);
        draw_arrow(s, a.x + a.w / 2 + o, a.y + a.h / 2 + o, dir, en ? col::BLACK : col::SHADOW);
    }
    Rect t = thumb();
    if (!t.empty()) {
        s.fill(t, col::FACE);
        s.bevel_raised(t);
    }
}

bool ScrollBar::mouse_down(int x, int y, bool& changed) {
    changed = false;
    if (!r.contains(x, y) || r.empty()) return false;
    int old = pos;
    int c = vertical ? y - r.y : x - r.x;
    int len = vertical ? r.h : r.w;
    Rect t = thumb();
    if (c < 16) { set(pos - line); pressed = 1; }
    else if (c >= len - 16) { set(pos + line); pressed = 2; }
    else if (!t.empty() && t.contains(x, y)) { drag = 1; drag_off = vertical ? y - t.y : x - t.x; }
    else if (!t.empty()) {
        int tc = vertical ? t.y : t.x;
        set((vertical ? y : x) < tc ? pos - page : pos + page);
    }
    changed = pos != old || pressed;
    return true;
}

void ScrollBar::mouse_move(int x, int y, bool& changed) {
    changed = false;
    if (!drag) return;
    int len = (vertical ? r.h : r.w) - 32;
    Rect t = thumb();
    int tl = vertical ? t.h : t.w;
    int start = (vertical ? y - r.y : x - r.x) - 16 - drag_off;
    int old = pos;
    if (len - tl > 0) set((int)((int64_t)start * max_pos() / (len - tl)));
    changed = pos != old;
}

void ScrollBar::mouse_up() { drag = 0; pressed = 0; }

static bool memchr_simple(const char* p, char c, int len) {
    for (int i = 0; i < len; i++) if (p[i] == c) return true;
    return false;
}

// ---------------------------------------------------------------------------
// Label
// ---------------------------------------------------------------------------
Label::Label(const Rect& rr, const char* t, int al) : text(t), align(al) { r = rr; }

void Label::paint(Surface& s) {
    Rect a{0, 0, r.w, r.h};
    if (bg != 0xFFFFFFFF) s.fill(a, bg);
    int x0 = 0;
    if (sunken) { s.bevel_thin_sunken(a); x0 = 3; }
    const char* p = text.c_str();
    int y = sunken ? (r.h - font->height) / 2 : 0;
    while (true) {
        const char* e = p;
        while (*e && *e != '\n') e++;
        int len = (int)(e - p);
        int w = font->text_width(p, len);
        int x = align == 0 ? x0 : (align == 1 ? (r.w - w) / 2 : r.w - w - x0);
        if (font == &font_ui && memchr_simple(p, '&', len)) {
            String part(p, (size_t)len);
            w = label_width_mn(part.c_str());
            x = align == 0 ? x0 : (align == 1 ? (r.w - w) / 2 : r.w - w - x0);
            s.text_mnemonic(*font, x, y, part.c_str(), enabled ? color : col::SHADOW);
        } else {
            s.text(*font, x, y, p, enabled ? color : col::SHADOW, len);
        }
        y += font->height;
        if (!*e) break;
        p = e + 1;
    }
}

// ---------------------------------------------------------------------------
// Button
// ---------------------------------------------------------------------------
Button::Button(const Rect& rr, const char* t, int i) : text(t) {
    r = rr;
    id = i;
    focusable = true;
}

void Button::paint(Surface& s) {
    Rect a{0, 0, r.w, r.h};
    bool sunk = (pressing && inside) || (toggle && down);
    s.fill(a, col::FACE);
    if (toggle && down && !pressing) {
        Rect in = a.inset(1);
        draw_checker(s, in);
    }
    if (flat) {
        if (sunk) s.bevel_thin_sunken(a);
        else if (hot || (pressing && inside)) s.bevel_thin_raised(a);
    } else if (sunk) {
        if (is_default || has_focus()) { s.rect(a, col::BLACK); s.rect(a.inset(1), col::SHADOW); }
        else s.bevel_pressed(a);
    } else if (is_default || has_focus()) {
        s.rect(a, col::BLACK);
        s.bevel_raised(a.inset(1));
    } else {
        s.bevel_raised(a);
    }
    int o = sunk ? 1 : 0;
    if (swatch != 0xFFFFFFFF) {
        s.fill(a.inset(3).offset(o, o), swatch);
        s.rect(a.inset(3).offset(o, o), col::SHADOW);
    }
    uint32_t tc = enabled ? col::BLACK : col::SHADOW;
    int tw = text.empty() ? 0 : label_width_mn(text.c_str());
    if (icon) {
        int total = icon->w + (tw ? tw + 4 : 0);
        int ix = (r.w - total) / 2 + o, iy = (r.h - icon->h) / 2 + o;
        if (tw && r.h > icon->h + font_ui.height + 4) {
            // icono arriba y texto abajo (barra de herramientas del Explorador)
            ix = (r.w - icon->w) / 2 + o;
            iy = 3 + o;
            draw_icon(s, *icon, ix, iy);
            s.text_mnemonic(font_ui, (r.w - tw) / 2 + o, iy + icon->h + 1, text.c_str(), tc);
        } else {
            draw_icon(s, *icon, ix, iy);
            if (tw) s.text_mnemonic(font_ui, ix + icon->w + 4, (r.h - font_ui.height) / 2 + o, text.c_str(), tc);
        }
    } else if (tw) {
        int tx = (r.w - tw) / 2 + o, ty = (r.h - font_ui.height) / 2 + o;
        if (!enabled) s.text_mnemonic(font_ui, tx + 1, ty + 1, text.c_str(), col::HILIGHT);
        s.text_mnemonic(font_ui, tx, ty, text.c_str(), tc);
    }
    if (has_focus() && !flat) s.dotted_rect(a.inset(4), col::BLACK);
}

int label_width_mn(const char* s) {
    int w = 0;
    for (int i = 0; s[i]; i++) {
        if (s[i] == '&' && s[i + 1] != '&') continue;
        w += font_ui.char_width((uint8_t)s[i]);
    }
    return w;
}

void Button::mouse_down(const MouseEv& e) {
    if (e.button != MB_LEFT) return;
    pressing = true;
    inside = true;
    invalidate();
}

void Button::mouse_move(const MouseEv& e) {
    bool in = e.x >= 0 && e.y >= 0 && e.x < r.w && e.y < r.h;
    if (pressing) {
        if (in != inside) { inside = in; invalidate(); }
    } else if (flat && in != hot) {
        hot = in;
        invalidate();
    }
}

void Button::mouse_up(const MouseEv& e) {
    if (!pressing) return;
    pressing = false;
    bool in = e.x >= 0 && e.y >= 0 && e.x < r.w && e.y < r.h;
    hot = false;
    invalidate();
    if (in && enabled) {
        if (toggle) down = true;
        notify();
    }
}

bool Button::key(const InputEvent& e) {
    if (e.pressed && (e.key == ' ' || e.key == KEY_ENTER)) {
        notify();
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// CheckBox
// ---------------------------------------------------------------------------
CheckBox::CheckBox(const Rect& rr, const char* t, int i, bool c) : text(t), checked(c) {
    r = rr;
    id = i;
    focusable = true;
}

void CheckBox::paint(Surface& s) {
    int by = (r.h - 13) / 2;
    if (radio) {
        Rect b{0, by, 12, 12};
        s.ellipse(b, col::WHITE, true);
        // borde hundido aproximado
        for (int i = 0; i < 12; i++)
            for (int j = 0; j < 12; j++) {
                int dx = 2 * i - 11, dy = 2 * j - 11;
                int d = dx * dx + dy * dy;
                if (d >= 100 && d <= 132) s.put(i, by + j, (i + j < 12) ? col::SHADOW : col::HILIGHT);
            }
        if (checked) s.fill({4, by + 4, 4, 4}, col::BLACK);
    } else {
        Rect b{0, by, 13, 13};
        s.fill(b, enabled ? col::WHITE : col::FACE);
        s.bevel_sunken(b);
        if (checked) {
            for (int k = 0; k < 3; k++) s.vline(3 + k, by + 5 + k, 3, col::BLACK);
            for (int k = 0; k < 4; k++) s.vline(6 + k, by + 6 - k, 3, col::BLACK);
        }
    }
    int tx = 18, ty = (r.h - font_ui.height) / 2;
    s.text_mnemonic(font_ui, tx, ty, text.c_str(), enabled ? col::BLACK : col::SHADOW);
    if (has_focus()) s.dotted_rect({tx - 2, ty - 1, label_width_mn(text.c_str()) + 4, font_ui.height + 2}, col::BLACK);
}

void CheckBox::mouse_up(const MouseEv& e) {
    if (e.x < 0 || e.y < 0 || e.x >= r.w || e.y >= r.h) return;
    checked = radio ? true : !checked;
    invalidate();
    notify();
}

bool CheckBox::key(const InputEvent& e) {
    if (e.pressed && e.key == ' ') {
        checked = radio ? true : !checked;
        invalidate();
        notify();
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// TextBox
// ---------------------------------------------------------------------------
TextBox::TextBox(const Rect& rr, const char* t, int i) : text(t) {
    r = rr;
    id = i;
    focusable = true;
    cpos = anchor = (int)text.size();
}

void TextBox::set_text(const char* t) {
    text = t;
    cpos = anchor = (int)text.size();
    scroll = 0;
    ensure_visible();
    invalidate();
}

void TextBox::select_all() {
    anchor = 0;
    cpos = (int)text.size();
    ensure_visible();
    invalidate();
}

void TextBox::paint(Surface& s) {
    Rect a{0, 0, r.w, r.h};
    s.fill(a, (enabled && !read_only) ? col::WHITE : col::FACE);
    s.bevel_sunken(a);
    PaintScope ps(s, {3, 2, r.w - 6, r.h - 4});
    int ty = (r.h - 4 - font_ui.height) / 2;
    int s0 = min(cpos, anchor), s1 = max(cpos, anchor);
    bool focus = has_focus();
    int x = -scroll;
    for (int i = 0; i < (int)text.size(); i++) {
        int cw = font_ui.char_width((uint8_t)text[i]);
        bool sel = focus && i >= s0 && i < s1;
        if (sel) s.fill({x, ty, cw, font_ui.height}, col::SELECT);
        s.text(font_ui, x, ty, text.c_str() + i, sel ? col::WHITE : (enabled ? col::BLACK : col::SHADOW), 1);
        x += cw;
    }
    if (focus && caret_on && !read_only) {
        int cx = font_ui.text_width(text.c_str(), cpos) - scroll;
        s.vline(cx, ty, font_ui.height, col::BLACK);
    }
}

int TextBox::pos_from_x(int x) {
    x += scroll - 3;
    int acc = 0;
    for (int i = 0; i < (int)text.size(); i++) {
        int cw = font_ui.char_width((uint8_t)text[i]);
        if (x < acc + cw / 2) return i;
        acc += cw;
    }
    return (int)text.size();
}

void TextBox::ensure_visible() {
    int cx = font_ui.text_width(text.c_str(), cpos);
    int vis = r.w - 8;
    if (cx - scroll > vis) scroll = cx - vis;
    if (cx - scroll < 0) scroll = cx;
    int tw = font_ui.text_width(text.c_str());
    if (tw - scroll < vis) scroll = max(0, tw - vis);
}

void TextBox::delete_selection() {
    int s0 = min(cpos, anchor), s1 = max(cpos, anchor);
    if (s0 == s1) return;
    text.erase((size_t)s0, (size_t)(s1 - s0));
    cpos = anchor = s0;
}

void TextBox::mouse_down(const MouseEv& e) {
    if (e.button != MB_LEFT) {
        return;
    }
    if (e.clicks == 2) { select_all(); return; }
    cpos = pos_from_x(e.x);
    if (!(e.mods & MOD_SHIFT)) anchor = cpos;
    selecting = true;
    caret_on = true;
    invalidate();
}

void TextBox::mouse_move(const MouseEv& e) {
    if (!selecting) return;
    int c = pos_from_x(e.x);
    if (c != cpos) { cpos = c; ensure_visible(); invalidate(); }
}

void TextBox::mouse_up(const MouseEv&) { selecting = false; }

bool TextBox::key(const InputEvent& e) {
    if (!e.pressed) return false;
    bool shift = e.mods & MOD_SHIFT;
    bool ctrl = e.mods & MOD_CTRL;
    int n = (int)text.size();
    bool changed = false;
    caret_on = true;
    if (ctrl) {
        if (e.key == 'a') { select_all(); return true; }
        if (e.key == 'c' || e.key == 'x') {
            int s0 = min(cpos, anchor), s1 = max(cpos, anchor);
            if (s0 != s1) {
                wm::clipboard() = text.substr((size_t)s0, (size_t)(s1 - s0));
                if (e.key == 'x' && !read_only) { delete_selection(); changed = true; }
            }
        } else if (e.key == 'v' && !read_only) {
            delete_selection();
            const String& c = wm::clipboard();
            for (size_t i = 0; i < c.size(); i++) {
                if (c[i] == '\r' || c[i] == '\n') break;
                text.insert((size_t)cpos++, &c[i], 1);
            }
            anchor = cpos;
            changed = true;
        } else {
            return false;
        }
    } else {
        switch (e.key) {
        case KEY_LEFT: if (cpos > 0) cpos--; if (!shift) anchor = cpos; break;
        case KEY_RIGHT: if (cpos < n) cpos++; if (!shift) anchor = cpos; break;
        case KEY_HOME: cpos = 0; if (!shift) anchor = cpos; break;
        case KEY_END: cpos = n; if (!shift) anchor = cpos; break;
        case KEY_BACKSPACE:
            if (read_only) break;
            if (cpos != anchor) delete_selection();
            else if (cpos > 0) { text.erase((size_t)cpos - 1, 1); cpos--; anchor = cpos; }
            changed = true;
            break;
        case KEY_DELETE:
            if (read_only) break;
            if (cpos != anchor) delete_selection();
            else if (cpos < n) text.erase((size_t)cpos, 1);
            changed = true;
            break;
        case KEY_ENTER:
            if (!id) return false; // sin receptor: lo gestiona la ventana (botón predeterminado)
            notify(1);
            return true;
        default:
            if (e.ch >= 32 && !read_only) {
                delete_selection();
                char c = (char)e.ch;
                text.insert((size_t)cpos, &c, 1);
                cpos++;
                anchor = cpos;
                changed = true;
            } else {
                return false;
            }
        }
    }
    ensure_visible();
    invalidate();
    if (changed) notify(2);
    return true;
}

void TextBox::focus_changed(bool f) {
    caret_on = true;
    if (f) select_all();
}

void TextBox::tick() {
    caret_on = !caret_on;
    invalidate();
}

// ---------------------------------------------------------------------------
// TextArea
// ---------------------------------------------------------------------------
TextArea::TextArea(const Rect& rr, int i) {
    r = rr;
    id = i;
    focusable = true;
    lines.push(String());
    vs.vertical = true;
    hs.vertical = false;
}

void TextArea::set_text(const char* t, size_t len) {
    lines.clear();
    String cur;
    for (size_t i = 0; i < len; i++) {
        char c = t[i];
        if (c == '\r') { if (i + 1 < len && t[i + 1] == '\n') i++; lines.push(move_val(cur)); cur = String(); continue; }
        if (c == '\n') { lines.push(move_val(cur)); cur = String(); continue; }
        if (c == 0) c = ' ';
        cur.append(c);
    }
    lines.push(move_val(cur));
    cy = cx = ay = ax = 0;
    top = left = 0;
    modified = false;
    invalidate();
}

String TextArea::get_text() const {
    String out;
    for (size_t i = 0; i < lines.size(); i++) {
        out += lines[i];
        if (i + 1 < lines.size()) out += T("\r\n");
    }
    return out;
}

size_t TextArea::text_size() const {
    size_t n = 0;
    for (size_t i = 0; i < lines.size(); i++) n += lines[i].size() + (i + 1 < lines.size() ? 2 : 0);
    return n;
}

int TextArea::col_to_x(int line, int c) const {
    const String& l = lines[line];
    int x = 0, sp = font->char_width(' ');
    for (int i = 0; i < c && i < (int)l.size(); i++) {
        if (l[i] == '\t') x = (x / (sp * 8) + 1) * sp * 8;
        else x += font->char_width((uint8_t)l[i]);
    }
    return x;
}

int TextArea::x_to_col(int line, int x) const {
    const String& l = lines[line];
    int acc = 0, sp = font->char_width(' ');
    for (int i = 0; i < (int)l.size(); i++) {
        int nx = l[i] == '\t' ? (acc / (sp * 8) + 1) * sp * 8 : acc + font->char_width((uint8_t)l[i]);
        if (x < (acc + nx) / 2) return i;
        acc = nx;
    }
    return (int)l.size();
}

Rect TextArea::text_rect() const {
    int w = r.w - 4 - (vs.needed() ? 16 : 0);
    int h = r.h - 4 - (hs.needed() ? 16 : 0);
    return {2, 2, w, h};
}

void TextArea::layout_scrollbars() {
    int maxw = 0;
    for (size_t i = 0; i < lines.size(); i++) maxw = max(maxw, col_to_x((int)i, (int)lines[i].size()));
    int vis_lines = 1, vis_w = 1;
    vs.total = (int)lines.size();
    hs.total = maxw + 8;
    // dos pasadas: cada barra puede hacer necesaria la otra
    for (int pass = 0; pass < 2; pass++) {
        Rect tr = text_rect();
        vis_lines = max(1, tr.h / line_h());
        vis_w = max(1, tr.w);
        vs.page = vis_lines;
        hs.page = vis_w;
    }
    Rect tr = text_rect();
    vs.r = {r.w - 18, 2, 16, tr.h};
    hs.r = {2, r.h - 18, tr.w, 16};
    hs.line = font->char_width('M') * 4;
    vs.pos = top;
    hs.pos = left;
}

void TextArea::paint(Surface& s) {
    layout_scrollbars();
    Rect a{0, 0, r.w, r.h};
    s.fill(a, bgc);
    s.bevel_sunken(a);
    Rect tr = text_rect();
    int y0, x0, y1, x1;
    order_sel(y0, x0, y1, x1);
    bool focus = has_focus();
    {
        PaintScope ps(s, tr);
        int lh = line_h(), sp = font->char_width(' ');
        int nvis = tr.h / lh + 1;
        for (int li = top; li < (int)lines.size() && li < top + nvis; li++) {
            int y = (li - top) * lh;
            const String& l = lines[li];
            int x = 2 - left;
            for (int i = 0; i <= (int)l.size(); i++) {
                bool sel = (li > y0 || (li == y0 && i >= x0)) && (li < y1 || (li == y1 && i < x1));
                if (i == (int)l.size()) {
                    if (sel && li < y1) s.fill({x, y, sp, lh}, col::SELECT);
                    break;
                }
                char c = l[i];
                int nx = c == '\t' ? 2 - left + ((x - 2 + left) / (sp * 8) + 1) * sp * 8 : x + font->char_width((uint8_t)c);
                if (nx < 0) { x = nx; continue; }
                if (x > tr.w) break;
                if (sel) s.fill({x, y, nx - x, lh}, col::SELECT);
                if (c != '\t') s.text(*font, x, y, &l[i], sel ? col::WHITE : fg, 1);
                x = nx;
            }
        }
        if (focus && caret_on && !read_only && cy >= top) {
            int x = 2 - left + col_to_x(cy, cx);
            s.vline(x, (cy - top) * lh, lh, fg);
        }
    }
    if (vs.needed()) vs.paint(s);
    if (hs.needed()) hs.paint(s);
    if (vs.needed() && hs.needed()) s.fill({r.w - 18, r.h - 18, 16, 16}, col::FACE);
}

void TextArea::order_sel(int& y0, int& x0, int& y1, int& x1) const {
    if (ay < cy || (ay == cy && ax <= cx)) { y0 = ay; x0 = ax; y1 = cy; x1 = cx; }
    else { y0 = cy; x0 = cx; y1 = ay; x1 = ax; }
}

void TextArea::ensure_visible() {
    layout_scrollbars();
    Rect tr = text_rect();
    int vis = max(1, tr.h / line_h());
    if (cy < top) top = cy;
    if (cy >= top + vis) top = cy - vis + 1;
    int x = col_to_x(cy, cx);
    if (x - left > tr.w - 8) left = x - tr.w + 32;
    if (x - left < 0) left = max(0, x - 32);
    top = clamp(top, 0, max(0, (int)lines.size() - 1));
}

void TextArea::move_cursor(int line, int c, bool extend) {
    cy = clamp(line, 0, (int)lines.size() - 1);
    cx = clamp(c, 0, (int)lines[cy].size());
    if (!extend) { ay = cy; ax = cx; }
    caret_on = true;
    ensure_visible();
    invalidate();
    notify(3);
}

void TextArea::changed() {
    modified = true;
    notify(2);
}

void TextArea::delete_selection() {
    if (!has_selection()) return;
    int y0, x0, y1, x1;
    order_sel(y0, x0, y1, x1);
    String tail = lines[y1].substr((size_t)x1);
    lines[y0].erase((size_t)x0, lines[y0].size());
    lines[y0] += tail;
    for (int i = y1; i > y0; i--) lines.erase((size_t)i);
    cy = ay = y0;
    cx = ax = x0;
}

void TextArea::insert(const char* s) {
    delete_selection();
    for (const char* p = s; *p; p++) {
        if (*p == '\r') continue;
        if (*p == '\n') {
            String tail = lines[cy].substr((size_t)cx);
            lines[cy].erase((size_t)cx, lines[cy].size());
            lines.insert((size_t)cy + 1, tail);
            cy++;
            cx = 0;
            continue;
        }
        // agrupa caracteres hasta el próximo salto de línea
        const char* e = p;
        while (*e && *e != '\n' && *e != '\r') e++;
        lines[cy].insert((size_t)cx, p, (size_t)(e - p));
        cx += (int)(e - p);
        p = e - 1;
    }
    ay = cy;
    ax = cx;
    ensure_visible();
    invalidate();
    changed();
}

void TextArea::select_all() {
    ay = 0;
    ax = 0;
    cy = (int)lines.size() - 1;
    cx = (int)lines[cy].size();
    ensure_visible();
    invalidate();
}

String TextArea::selected_text() const {
    String out;
    int y0, x0, y1, x1;
    order_sel(y0, x0, y1, x1);
    for (int y = y0; y <= y1; y++) {
        int a = y == y0 ? x0 : 0;
        int b = y == y1 ? x1 : (int)lines[y].size();
        out += lines[y].substr((size_t)a, (size_t)(b - a));
        if (y < y1) out += T("\r\n");
    }
    return out;
}

void TextArea::copy() { if (has_selection()) wm::clipboard() = selected_text(); }
void TextArea::cut() {
    if (!has_selection() || read_only) return;
    copy();
    delete_selection();
    ensure_visible();
    invalidate();
    changed();
}
void TextArea::paste() {
    if (read_only || wm::clipboard().empty()) return;
    insert(wm::clipboard().c_str());
}

bool TextArea::find(const char* needle, bool match_case, bool down) {
    int n = (int)strlen(needle);
    if (!n) return false;
    auto match_at = [&](const String& l, int i) {
        if (i < 0 || i + n > (int)l.size()) return false;
        for (int k = 0; k < n; k++) {
            char a = l[i + k], b = needle[k];
            if (match_case ? a != b : to_upper(a) != to_upper(b)) return false;
        }
        return true;
    };
    int y0, x0, y1, x1;
    order_sel(y0, x0, y1, x1);
    if (down) {
        int sy = y1, sx = x1;
        for (int y = sy; y < (int)lines.size(); y++)
            for (int i = (y == sy ? sx : 0); i + n <= (int)lines[y].size(); i++)
                if (match_at(lines[y], i)) { ay = y; ax = i; cy = y; cx = i + n; ensure_visible(); invalidate(); return true; }
    } else {
        for (int y = y0; y >= 0; y--)
            for (int i = (y == y0 ? x0 - n : (int)lines[y].size() - n); i >= 0; i--)
                if (match_at(lines[y], i)) { ay = y; ax = i; cy = y; cx = i + n; ensure_visible(); invalidate(); return true; }
    }
    return false;
}

void TextArea::goto_line(int line) { move_cursor(line, 0, false); }

Cursor TextArea::cursor(int x, int y) {
    if ((vs.needed() && vs.r.contains(x, y)) || (hs.needed() && hs.r.contains(x, y))) return CURSOR_ARROW;
    return CURSOR_IBEAM;
}

void TextArea::mouse_down(const MouseEv& e) {
    layout_scrollbars();
    bool ch;
    if (vs.needed() && vs.mouse_down(e.x, e.y, ch)) { sb_capture = 1; top = vs.pos; invalidate(); return; }
    if (hs.needed() && hs.mouse_down(e.x, e.y, ch)) { sb_capture = 2; left = hs.pos; invalidate(); return; }
    if (e.button != MB_LEFT) return;
    Rect tr = text_rect();
    int line = clamp(top + (e.y - tr.y) / line_h(), 0, (int)lines.size() - 1);
    int c = x_to_col(line, e.x - tr.x - 2 + left);
    if (e.clicks == 2) {
        // selecciona la palabra
        const String& l = lines[line];
        int a = c, b = c;
        auto word = [](char ch) { return ch != ' ' && ch != '\t' && ch != '.' && ch != ',' && ch != ';'; };
        while (a > 0 && word(l[a - 1])) a--;
        while (b < (int)l.size() && word(l[b])) b++;
        ay = line; ax = a; cy = line; cx = b;
        invalidate();
        return;
    }
    move_cursor(line, c, e.mods & MOD_SHIFT);
    selecting = true;
}

void TextArea::mouse_move(const MouseEv& e) {
    bool ch;
    if (sb_capture == 1) { vs.mouse_move(e.x, e.y, ch); if (ch) { top = vs.pos; invalidate(); } return; }
    if (sb_capture == 2) { hs.mouse_move(e.x, e.y, ch); if (ch) { left = hs.pos; invalidate(); } return; }
    if (!selecting) return;
    Rect tr = text_rect();
    int line = clamp(top + (e.y - tr.y) / line_h(), 0, (int)lines.size() - 1);
    if (e.y < tr.y) line = max(0, top - 1);
    int c = x_to_col(line, e.x - tr.x - 2 + left);
    move_cursor(line, c, true);
}

void TextArea::mouse_up(const MouseEv&) {
    if (sb_capture) { vs.mouse_up(); hs.mouse_up(); sb_capture = 0; invalidate(); }
    selecting = false;
}

void TextArea::mouse_wheel(const MouseEv& e) {
    layout_scrollbars();
    vs.set(top + e.wheel * 3);
    top = vs.pos;
    invalidate();
}

bool TextArea::key(const InputEvent& e) {
    if (!e.pressed) return false;
    bool shift = e.mods & MOD_SHIFT, ctrl = e.mods & MOD_CTRL;
    Rect tr = text_rect();
    int vis = max(1, tr.h / line_h());
    if (ctrl) {
        switch (e.key) {
        case 'a': case 'e':
            if (e.key == (i18n::language() == Lang::EN ? 'a' : 'e')) { select_all(); return true; }
            return false;
        case 'c': copy(); return true;
        case 'x': cut(); return true;
        case 'v': paste(); return true;
        case KEY_HOME: move_cursor(0, 0, shift); return true;
        case KEY_END: move_cursor((int)lines.size() - 1, (int)lines.back().size(), shift); return true;
        case KEY_LEFT: {
            int y = cy, x = cx;
            if (x == 0 && y > 0) { y--; x = (int)lines[y].size(); }
            else { while (x > 0 && lines[y][x - 1] == ' ') x--; while (x > 0 && lines[y][x - 1] != ' ') x--; }
            move_cursor(y, x, shift);
            return true;
        }
        case KEY_RIGHT: {
            int y = cy, x = cx;
            int n = (int)lines[y].size();
            if (x == n && y + 1 < (int)lines.size()) { y++; x = 0; }
            else { while (x < n && lines[y][x] != ' ') x++; while (x < n && lines[y][x] == ' ') x++; }
            move_cursor(y, x, shift);
            return true;
        }
        }
        return false;
    }
    switch (e.key) {
    case KEY_LEFT:
        if (!shift && has_selection()) { int y0, x0, y1, x1; order_sel(y0, x0, y1, x1); move_cursor(y0, x0, false); }
        else if (cx > 0) move_cursor(cy, cx - 1, shift);
        else if (cy > 0) move_cursor(cy - 1, (int)lines[cy - 1].size(), shift);
        return true;
    case KEY_RIGHT:
        if (!shift && has_selection()) { int y0, x0, y1, x1; order_sel(y0, x0, y1, x1); move_cursor(y1, x1, false); }
        else if (cx < (int)lines[cy].size()) move_cursor(cy, cx + 1, shift);
        else if (cy + 1 < (int)lines.size()) move_cursor(cy + 1, 0, shift);
        return true;
    case KEY_UP: if (cy > 0) move_cursor(cy - 1, x_to_col(cy - 1, col_to_x(cy, cx)), shift); return true;
    case KEY_DOWN: if (cy + 1 < (int)lines.size()) move_cursor(cy + 1, x_to_col(cy + 1, col_to_x(cy, cx)), shift); return true;
    case KEY_HOME: move_cursor(cy, 0, shift); return true;
    case KEY_END: move_cursor(cy, (int)lines[cy].size(), shift); return true;
    case KEY_PGUP: top = max(0, top - vis); move_cursor(cy - vis, cx, shift); return true;
    case KEY_PGDN: top = min(max(0, (int)lines.size() - vis), top + vis); move_cursor(cy + vis, cx, shift); return true;
    }
    if (read_only) return false;
    switch (e.key) {
    case KEY_BACKSPACE:
        if (has_selection()) delete_selection();
        else if (cx > 0) { lines[cy].erase((size_t)cx - 1, 1); cx--; }
        else if (cy > 0) {
            int nx = (int)lines[cy - 1].size();
            lines[cy - 1] += lines[cy];
            lines.erase((size_t)cy);
            cy--;
            cx = nx;
        } else return true;
        ax = cx; ay = cy;
        ensure_visible(); invalidate(); changed();
        return true;
    case KEY_DELETE:
        if (has_selection()) delete_selection();
        else if (cx < (int)lines[cy].size()) lines[cy].erase((size_t)cx, 1);
        else if (cy + 1 < (int)lines.size()) { lines[cy] += lines[cy + 1]; lines.erase((size_t)cy + 1); }
        else return true;
        ensure_visible(); invalidate(); changed();
        return true;
    case KEY_ENTER: insert(T("\n")); return true;
    case KEY_TAB: insert(T("\t")); return true;
    }
    if (e.ch >= 32) {
        char buf[2] = {(char)e.ch, 0};
        insert(buf);
        return true;
    }
    return false;
}

void TextArea::focus_changed(bool) { caret_on = true; }
void TextArea::tick() { caret_on = !caret_on; invalidate(); }

// ---------------------------------------------------------------------------
// ListView
// ---------------------------------------------------------------------------
ListView::ListView(const Rect& rr, int i) {
    r = rr;
    id = i;
    focusable = true;
    vs.vertical = true;
}

void ListView::clear() {
    items.clear();
    focus_item = anchor_item = -1;
    scroll = 0;
    invalidate();
}

ListItem& ListView::add(const char* text, const IconPair* icon, int64_t data) {
    ListItem it;
    it.text = text;
    it.icon = icon;
    it.data = data;
    items.push(move_val(it));
    invalidate();
    return items.back();
}

int ListView::selected() const {
    for (size_t i = 0; i < items.size(); i++) if (items[i].selected) return (int)i;
    return -1;
}

int ListView::selected_count() const {
    int n = 0;
    for (auto& it : items) if (it.selected) n++;
    return n;
}

void ListView::select(int i, bool only) {
    if (only) for (auto& it : items) it.selected = false;
    if (i >= 0 && i < (int)items.size()) {
        items[i].selected = true;
        focus_item = i;
        anchor_item = i;
        ensure_visible(i);
    }
    invalidate();
}

int ListView::per_row() const {
    int w = r.w - 4 - 16;
    return max(1, w / cell_w());
}

void ListView::layout() {
    vs.r = {r.w - 18, 2, 16, r.h - 4};
    vs.page = r.h - 4 - header_h();
    vs.total = content_h();
    vs.line = cell_h();
    if (scroll > vs.max_pos()) scroll = vs.max_pos();
    if (scroll < 0) scroll = 0;
    vs.pos = scroll;
}

int ListView::content_h() const {
    int n = (int)items.size();
    if (mode == ICONS) return ((n + per_row() - 1) / per_row()) * cell_h() + 4;
    return n * cell_h() + 2;
}

Rect ListView::item_rect(int i) const {
    int top = 2 + header_h();
    if (mode == ICONS) {
        int pr = per_row();
        return {2 + (i % pr) * cell_w(), top + 2 + (i / pr) * cell_h() - scroll, cell_w(), cell_h()};
    }
    return {2, top + i * cell_h() - scroll, r.w - 4 - (vs.needed() ? 16 : 0), cell_h()};
}

int ListView::item_at(int x, int y) const {
    if (y < 2 + header_h()) return -1;
    for (int i = 0; i < (int)items.size(); i++) {
        Rect ir = item_rect(i);
        if (mode == ICONS) {
            // zona sensible: icono + etiqueta
            Rect hit{ir.x + 8, ir.y, ir.w - 16, ir.h - 4};
            if (hit.contains(x, y)) return i;
        } else if (mode == DETAILS) {
            int w0 = columns.size() ? columns[0].width : ir.w;
            if (Rect{ir.x, ir.y, w0, ir.h}.contains(x, y)) return i;
        } else if (ir.contains(x, y)) {
            return i;
        }
    }
    return -1;
}

void ListView::ensure_visible(int i) {
    layout();
    Rect ir = item_rect(i);
    int top = 2 + header_h(), bottom = r.h - 2;
    if (ir.y < top) scroll -= top - ir.y;
    else if (ir.bottom() > bottom) scroll += ir.bottom() - bottom;
    layout();
}

void ListView::paint(Surface& s) {
    layout();
    Rect a{0, 0, r.w, r.h};
    s.fill(a, col::WHITE);
    s.bevel_sunken(a);
    int iw = r.w - 4 - (vs.needed() ? 16 : 0);
    bool focus = has_focus();
    if (mode == DETAILS) {
        int x = 2;
        PaintScope hdr(s, {2, 2, iw, 18});
        for (auto& c : columns) {
            Rect hr{x - 2, 0, c.width, 18};
            s.fill(hr, col::FACE);
            s.bevel_raised(hr);
            s.text(font_ui, hr.x + 5, 3, c.title.c_str(), col::BLACK);
            x += c.width;
        }
        if (x - 2 < iw) { Rect hr{x - 2, 0, iw - x + 2, 18}; s.fill(hr, col::FACE); s.bevel_raised(hr); }
    }
    {
        PaintScope body(s, {2, 2 + header_h(), iw, r.h - 4 - header_h()});
        s.ox -= 2;
        s.oy -= 2 + header_h();
        for (int i = 0; i < (int)items.size(); i++) {
            Rect ir = item_rect(i);
            if (ir.bottom() < 2 + header_h() || ir.y > r.h) continue;
            const ListItem& it = items[i];
            if (mode == ICONS) {
                draw_icon_label(s, ir, it.icon, it.text.c_str(), it.selected, focus && i == focus_item, false, 0xFFFFFFFF);
                continue;
            }
            int x = ir.x + 2;
            if (it.icon) {
                if (it.selected) draw_icon_selected(s, it.icon->small, x, ir.y + 1);
                else draw_icon(s, it.icon->small, x, ir.y + 1);
            }
            int c0w = mode == DETAILS && columns.size() ? columns[0].width : ir.w;
            int tw = min(font_ui.text_width(it.text.c_str()), c0w - 24);
            Rect tr{x + 18, ir.y + 1, tw + 4, ir.h - 2};
            if (it.selected) s.fill(tr, focus ? col::SELECT : col::FACE);
            {
                PaintScope ps(s, {ir.x, ir.y, c0w - 4, ir.h});
                s.text(font_ui, x + 20 - ir.x, (ir.h - font_ui.height) / 2, it.text.c_str(),
                       it.selected && focus ? col::WHITE : col::BLACK);
            }
            if (focus && i == focus_item) s.dotted_rect(tr, col::BLACK);
            if (mode == DETAILS) {
                int cx = ir.x + c0w;
                for (size_t c = 1; c < columns.size(); c++) {
                    if (c - 1 < it.sub.size()) {
                        const char* t = it.sub[c - 1].c_str();
                        PaintScope ps(s, {cx, ir.y, columns[c].width - 6, ir.h});
                        int tx = columns[c].right ? columns[c].width - 8 - font_ui.text_width(t) : 4;
                        s.text(font_ui, tx, (ir.h - font_ui.height) / 2, t, col::BLACK);
                    }
                    cx += columns[c].width;
                }
            }
        }
    }
    if (vs.needed()) vs.paint(s);
}

void ListView::mouse_down(const MouseEv& e) {
    layout();
    bool ch;
    if (vs.needed() && vs.mouse_down(e.x, e.y, ch)) { sb_capture = 1; scroll = vs.pos; invalidate(); return; }
    if (mode == DETAILS && e.y >= 2 && e.y < 2 + header_h()) {
        int x = 2;
        for (size_t c = 0; c < columns.size(); c++) {
            if (e.x >= x && e.x < x + columns[c].width) {
                clicked_column = (int)c;
                notify(LV_COLUMN);
                return;
            }
            x += columns[c].width;
        }
        return;
    }
    int i = item_at(e.x, e.y);
    bool ctrl = e.mods & MOD_CTRL, shift = e.mods & MOD_SHIFT;
    if (e.button == MB_RIGHT) {
        if (i >= 0 && !items[i].selected) select(i, true);
        if (i < 0) { for (auto& it : items) it.selected = false; focus_item = -1; }
        ctx_x = e.sx;
        ctx_y = e.sy;
        invalidate();
        notify(LV_SELCHANGE);
        notify(LV_CONTEXT);
        return;
    }
    if (e.button != MB_LEFT) return;
    if (i < 0) {
        if (!ctrl) for (auto& it : items) it.selected = false;
        focus_item = -1;
        invalidate();
        notify(LV_SELCHANGE);
        return;
    }
    if (shift && anchor_item >= 0) {
        for (auto& it : items) it.selected = false;
        for (int k = min(anchor_item, i); k <= max(anchor_item, i); k++) items[k].selected = true;
        focus_item = i;
    } else if (ctrl) {
        items[i].selected = !items[i].selected;
        focus_item = anchor_item = i;
    } else {
        select(i, true);
    }
    invalidate();
    notify(LV_SELCHANGE);
    if (e.clicks == 2) notify(LV_ACTIVATE);
}

void ListView::mouse_move(const MouseEv& e) {
    if (sb_capture) {
        bool ch;
        vs.mouse_move(e.x, e.y, ch);
        if (ch) { scroll = vs.pos; invalidate(); }
    }
}

void ListView::mouse_up(const MouseEv&) {
    if (sb_capture) { vs.mouse_up(); sb_capture = 0; invalidate(); }
}

void ListView::mouse_wheel(const MouseEv& e) {
    layout();
    vs.set(scroll + e.wheel * cell_h() * (mode == ICONS ? 1 : 3));
    scroll = vs.pos;
    invalidate();
}

bool ListView::key(const InputEvent& e) {
    if (!e.pressed) return false;
    int n = (int)items.size();
    int f = focus_item;
    int step_v = mode == ICONS ? per_row() : 1;
    int nf = f;
    switch (e.key) {
    case KEY_LEFT: nf = mode == ICONS ? f - 1 : f; break;
    case KEY_RIGHT: nf = mode == ICONS ? f + 1 : f; break;
    case KEY_UP: nf = f - step_v; break;
    case KEY_DOWN: nf = f < 0 ? 0 : f + step_v; break;
    case KEY_HOME: nf = 0; break;
    case KEY_END: nf = n - 1; break;
    case KEY_ENTER: if (selected() >= 0) notify(LV_ACTIVATE); return true;
    case KEY_DELETE: notify(LV_DELETE); return true;
    case KEY_F2: notify(LV_RENAME); return true;
    case KEY_BACKSPACE: notify(LV_BACK); return true;
    default:
        if ((e.mods & MOD_CTRL) && e.key == 'a') {
            for (auto& it : items) it.selected = true;
            invalidate();
            notify(LV_SELCHANGE);
            return true;
        }
        if (e.ch > 32 && !(e.mods & MOD_CTRL)) {
            // salta al siguiente elemento que empiece por esa letra
            for (int k = 1; k <= n; k++) {
                int idx = (f + k + n) % n;
                if (n && to_upper(items[idx].text[0]) == to_upper((char)e.ch)) { select(idx); notify(LV_SELCHANGE); return true; }
            }
            return true;
        }
        last_key = e.key;
        notify(LV_KEY);
        return false;
    }
    if (n == 0) return true;
    nf = clamp(nf, 0, n - 1);
    if (e.mods & MOD_SHIFT && anchor_item >= 0) {
        for (auto& it : items) it.selected = false;
        for (int k = min(anchor_item, nf); k <= max(anchor_item, nf); k++) items[k].selected = true;
        focus_item = nf;
        ensure_visible(nf);
    } else {
        select(nf);
    }
    invalidate();
    notify(LV_SELCHANGE);
    return true;
}

// ---------------------------------------------------------------------------
// ProgressBar
// ---------------------------------------------------------------------------
void ProgressBar::paint(Surface& s) {
    Rect a{0, 0, r.w, r.h};
    s.fill(a, col::FACE);
    s.bevel_thin_sunken(a);
    int inner = r.w - 6;
    int filled = maximum > 0 ? (int)((int64_t)inner * clamp(value, 0, maximum) / maximum) : 0;
    for (int x = 0; x < filled; x += 10)
        s.fill({3 + x, 3, min(8, filled - x), r.h - 6}, col::SELECT);
}

// ---------------------------------------------------------------------------
// TrackBar
// ---------------------------------------------------------------------------
namespace {
constexpr int THUMB_LONG = 21, THUMB_SHORT = 11, TRACK_MARGIN = 8;
}

TrackBar::TrackBar(const Rect& rr, int i, bool v) : vertical(v) {
    r = rr;
    id = i;
    focusable = true;
}

int TrackBar::track_len() const { return (vertical ? r.h : r.w) - 2 * TRACK_MARGIN; }

int TrackBar::value_to_pos(int v) const {
    int span = max(1, maximum - minimum);
    int p = (v - minimum) * track_len() / span;
    return TRACK_MARGIN + (vertical ? track_len() - p : p);
}

int TrackBar::pos_to_value(int p) const {
    int span = max(1, maximum - minimum);
    int off = clamp(p - TRACK_MARGIN, 0, track_len());
    if (vertical) off = track_len() - off;
    return minimum + (off * span + track_len() / 2) / max(1, track_len());
}

void TrackBar::set(int v) {
    value = clamp(v, minimum, maximum);
    invalidate();
}

void TrackBar::change(int v) {
    v = clamp(v, minimum, maximum);
    if (v == value) return;
    value = v;
    invalidate();
    notify(1);
}

void TrackBar::paint(Surface& s) {
    int pos = value_to_pos(value);
    if (vertical) {
        int cx = r.w / 2 - 6;
        Rect ch{cx - 2, TRACK_MARGIN - 2, 4, track_len() + 4};
        s.fill(ch, col::WHITE);
        s.bevel_thin_sunken(ch);
        if (tick_every > 0)
            for (int v = minimum; v <= maximum; v += tick_every) s.hline(cx + 14, value_to_pos(v), 4, col::BLACK);
        Rect th{cx - THUMB_LONG / 2, pos - THUMB_SHORT / 2, THUMB_LONG, THUMB_SHORT};
        s.fill(th, col::FACE);
        s.bevel_raised(th);
    } else {
        int cy = r.h / 2 - 6;
        Rect ch{TRACK_MARGIN - 2, cy - 2, track_len() + 4, 4};
        s.fill(ch, col::WHITE);
        s.bevel_thin_sunken(ch);
        if (tick_every > 0)
            for (int v = minimum; v <= maximum; v += tick_every) s.vline(value_to_pos(v), cy + 14, 4, col::BLACK);
        Rect th{pos - THUMB_SHORT / 2, cy - THUMB_LONG / 2, THUMB_SHORT, THUMB_LONG};
        s.fill(th, col::FACE);
        s.bevel_raised(th);
    }
    if (has_focus()) s.dotted_rect({0, 0, r.w, r.h}, col::BLACK);
}

void TrackBar::mouse_down(const MouseEv& e) {
    if (!enabled) return;
    int p = vertical ? e.y : e.x;
    int pos = value_to_pos(value);
    if (abs_val(p - pos) <= THUMB_SHORT / 2 + 1) {
        dragging = true;
        return;
    }
    // clic en el canal: salta una página hacia el clic
    bool towards_max = vertical ? p < pos : p > pos;
    change(value + (towards_max ? page : -page));
}

void TrackBar::mouse_move(const MouseEv& e) {
    if (dragging && (e.buttons & MB_LEFT)) change(pos_to_value(vertical ? e.y : e.x));
}

void TrackBar::mouse_up(const MouseEv&) { dragging = false; }

void TrackBar::mouse_wheel(const MouseEv& e) {
    if (enabled) change(value + (e.wheel > 0 ? 1 : -1) * max(1, page / 2));
}

bool TrackBar::key(const InputEvent& e) {
    if (!e.pressed || !enabled) return false;
    switch (e.key) {
    case KEY_RIGHT: case KEY_UP: change(value + 1); return true;
    case KEY_LEFT: case KEY_DOWN: change(value - 1); return true;
    case KEY_PGUP: change(value + page); return true;
    case KEY_PGDN: change(value - page); return true;
    case KEY_HOME: change(vertical ? maximum : minimum); return true;
    case KEY_END: change(vertical ? minimum : maximum); return true;
    }
    return false;
}

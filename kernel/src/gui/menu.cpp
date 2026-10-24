#include "gui/menu.h"
#include "arch/idt.h"
#include "lib/i18n.h"

// ---------------------------------------------------------------------------
// Menu
// ---------------------------------------------------------------------------
namespace {
Vec<Menu*>& all_menus() {
    static Vec<Menu*>* v = new Vec<Menu*>();
    return *v;
}
}

Menu::Menu() { all_menus().push(this); }

Menu::~Menu() {
    for (auto& it : items) delete it.sub;
    all_menus().remove(this);
}

void Menu::retranslate() {
    for (auto& it : items) {
        it.label = i18n::retranslate(it.label.c_str());
        if (!it.shortcut.empty()) it.shortcut = i18n::retranslate(it.shortcut.c_str());
    }
}

void Menu::retranslate_all() {
    for (Menu* m : all_menus()) m->retranslate();
}

void MenuBar::retranslate() {
    for (auto& t : titles) t = i18n::retranslate(t.c_str());
}

void Menu::clear() {
    for (auto& it : items) delete it.sub;
    items.clear();
}

Menu& Menu::add(const char* label, int id, const char* shortcut, const IconPair* icon) {
    MenuItem it;
    it.label = label;
    it.id = id;
    if (shortcut) it.shortcut = shortcut;
    it.icon = icon;
    items.push(move_val(it));
    return *this;
}

Menu& Menu::submenu(const char* label, Menu* sub, const IconPair* icon) {
    MenuItem it;
    it.label = label;
    it.sub = sub;
    it.icon = icon;
    items.push(move_val(it));
    return *this;
}

Menu& Menu::separator() {
    MenuItem it;
    it.separator = true;
    items.push(move_val(it));
    return *this;
}

MenuItem* Menu::find(int id) {
    for (auto& it : items) {
        if (!it.separator && !it.sub && it.id == id) return &it;
        if (it.sub) if (MenuItem* r = it.sub->find(id)) return r;
    }
    return nullptr;
}

void Menu::check(int id, bool on) { if (MenuItem* i = find(id)) i->checked = on; }
void Menu::enable(int id, bool on) { if (MenuItem* i = find(id)) i->disabled = !on; }

// ---------------------------------------------------------------------------
// Barra de menús
// ---------------------------------------------------------------------------
MenuBar::~MenuBar() {
    for (Menu* m : menus) delete m;
}

Menu* MenuBar::add(const char* title) {
    titles.push(String(title));
    Menu* m = new Menu();
    menus.push(m);
    return m;
}

static int label_width(const char* s) {
    int w = 0;
    for (int i = 0; s[i]; i++) {
        if (s[i] == '&' && s[i + 1] != '&') continue;
        w += font_ui.char_width((uint8_t)s[i]);
    }
    return w;
}

int MenuBar::item_w(int i) const { return label_width(titles[i].c_str()) + 14; }
int MenuBar::item_x(int i) const {
    int x = 2;
    for (int k = 0; k < i; k++) x += item_w(k);
    return x;
}

void attach_menubar(Window* w, MenuBar* mb) {
    w->menubar = mb;
    w->frame.h += MENUBAR_H;
}

void delete_menubar(MenuBar* mb) { delete mb; }

void paint_menubar(Window* w, Surface& s, int width) {
    MenuBar* mb = w->menubar;
    s.fill({0, 0, width, MENUBAR_H}, col::FACE);
    for (size_t i = 0; i < mb->titles.size(); i++) {
        int x = mb->item_x((int)i), iw = mb->item_w((int)i);
        if (mb->open_index == (int)i) s.bevel_thin_sunken({x, 1, iw, MENUBAR_H - 2});
        int o = mb->open_index == (int)i ? 1 : 0;
        s.text_mnemonic(font_ui, x + 7 + o, 3 + o, mb->titles[i].c_str(), col::BLACK);
    }
}

static void open_bar_menu(Window* w, int i) {
    MenuBar* mb = w->menubar;
    if (i < 0 || i >= (int)mb->menus.size()) return;
    w->on_menu_init(mb->menus[i]);
    Rect cr = w->client_rect();
    PopupMenu* p = show_menu(mb->menus[i], w, w->frame.x + w->border() + mb->item_x(i), w->frame.y + cr.y - 1);
    p->from_bar = mb;
    p->bar_index = i;
    mb->open_index = i;
    w->invalidate();
}

static int bar_index_at(MenuBar* mb, int x) {
    for (size_t i = 0; i < mb->titles.size(); i++) {
        int ix = mb->item_x((int)i);
        if (x >= ix && x < ix + mb->item_w((int)i)) return (int)i;
    }
    return -1;
}

void menubar_click(Window* w, int x, int) {
    MenuBar* mb = w->menubar;
    int i = bar_index_at(mb, x);
    if (i < 0) return;
    if (mb->closed_index == i && pit::ticks() - mb->closed_time < 3) { mb->closed_index = -1; return; }
    wm::close_popups();
    open_bar_menu(w, i);
}

void menubar_hover(Window* w, int x) {
    MenuBar* mb = w->menubar;
    if (mb->open_index < 0) return;
    int i = bar_index_at(mb, x);
    if (i < 0 || i == mb->open_index) return;
    wm::close_popups();
    open_bar_menu(w, i);
}

bool menubar_mnemonic(Window* w, char c) {
    MenuBar* mb = w->menubar;
    for (size_t i = 0; i < mb->titles.size(); i++) {
        const char* t = mb->titles[i].c_str();
        for (int k = 0; t[k]; k++) {
            if (t[k] == '&' && to_upper(t[k + 1]) == to_upper(c)) {
                wm::close_popups();
                open_bar_menu(w, (int)i);
                return true;
            }
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Menú emergente
// ---------------------------------------------------------------------------
namespace {
constexpr int SMALL_H = 18, SEP_H = 8, LARGE_H = 36, BANNER_W = 22;

bool menu_has_icons(Menu* m) {
    for (auto& it : m->items) if (it.icon) return true;
    return false;
}
}

PopupMenu::PopupMenu(Menu* m, Window* t, bool lg)
    : Window("", 10, 10, WF_POPUP | WF_NO_TASKBAR), menu(m), target(t), large(lg) {
    owner = t;
    bool icons = menu_has_icons(m);
    int text_w = 0, sc_w = 0;
    bool any_sub = false;
    for (auto& it : m->items) {
        if (it.separator) continue;
        text_w = max(text_w, label_width(it.label.c_str()));
        if (!it.shortcut.empty()) sc_w = max(sc_w, font_ui.text_width(it.shortcut.c_str()));
        if (it.sub) any_sub = true;
    }
    int left = large ? BANNER_W + 3 + 44 : (icons ? 26 : 20);
    int w = left + text_w + (sc_w ? sc_w + 24 : 0) + (any_sub ? 20 : 12) + 3;
    if (large) w = max(w, 190);
    int h = 3;
    for (size_t i = 0; i < m->items.size(); i++) h += item_h((int)i);
    h += 3;
    frame = {0, 0, w, h};
}

void PopupMenu::on_destroyed() {
    if (parent && parent->child == this) parent->child = nullptr;
    if (from_bar) {
        from_bar->open_index = -1;
        from_bar->closed_index = bar_index;
        from_bar->closed_time = pit::ticks();
        if (target && !target->closing) target->invalidate();
    }
}

int PopupMenu::item_h(int i) const {
    const MenuItem& it = menu->items[i];
    if (it.separator) return SEP_H;
    if (large) return LARGE_H;
    return menu_has_icons(menu) ? 22 : SMALL_H;
}

int PopupMenu::item_y(int i) const {
    int y = 3;
    for (int k = 0; k < i; k++) y += item_h(k);
    return y;
}

int PopupMenu::item_at(int y) const {
    int iy = 3;
    for (size_t i = 0; i < menu->items.size(); i++) {
        int h = item_h((int)i);
        if (y >= iy && y < iy + h) return menu->items[i].separator ? -1 : (int)i;
        iy += h;
    }
    return -1;
}

void PopupMenu::on_paint(Surface& s) {
    Rect r{0, 0, frame.w, frame.h};
    s.fill(r, col::FACE);
    s.bevel_raised(r);
    int x0 = 3;
    if (large) {
        // Banda lateral con el nombre del sistema en vertical
        Rect band{3, 3, BANNER_W, frame.h - 6};
        for (int y = 0; y < band.h; y++)
            s.hline(band.x, band.y + y, band.w, blend(col::TITLE_A2, col::TITLE_A1, y * 256 / max(1, band.h)));
        const char* txt = T("BrysOS 98");
        const Font& f = font_large;
        int tw = f.text_width(txt);
        Surface tmp(tw, f.height);
        tmp.clear(0);
        tmp.text(f, 0, 0, txt, 0xFFFFFF);
        int bx = band.x + (band.w - f.height) / 2 + 1, by = band.bottom() - 6;
        for (int y = 0; y < f.height; y++)
            for (int x = 0; x < tw; x++)
                if (tmp.px[y * tmp.stride + x]) s.put(bx + y, by - x, col::WHITE);
        x0 = band.right();
    }
    bool icons = menu_has_icons(menu);
    for (size_t i = 0; i < menu->items.size(); i++) {
        const MenuItem& it = menu->items[i];
        int y = item_y((int)i), h = item_h((int)i);
        if (it.separator) {
            s.hline(x0 + 1, y + 3, frame.w - x0 - 5, col::SHADOW);
            s.hline(x0 + 1, y + 4, frame.w - x0 - 5, col::HILIGHT);
            continue;
        }
        bool hl = (int)i == hover;
        Rect row{x0, y, frame.w - x0 - 3, h};
        if (hl) s.fill(row, col::SELECT);
        uint32_t tc = it.disabled ? col::SHADOW : (hl ? col::WHITE : col::BLACK);
        int tx;
        if (large) {
            if (it.icon) draw_icon(s, it.icon->big, x0 + 6, y + 2);
            tx = x0 + 46;
        } else {
            if (it.icon) draw_icon(s, it.icon->small, x0 + 3, y + (h - 16) / 2);
            tx = x0 + (icons ? 24 : 18);
        }
        if (it.checked) {
            // marca de verificación
            int cx = x0 + 6, cy = y + h / 2 - 3;
            for (int k = 0; k < 3; k++) { s.vline(cx + k, cy + 2 + k, 3, tc); }
            for (int k = 0; k < 4; k++) { s.vline(cx + 3 + k, cy + 3 - k, 3, tc); }
        }
        int ty = y + (h - font_ui.height) / 2;
        if (it.disabled && !hl) s.text_mnemonic(font_ui, tx + 1, ty + 1, it.label.c_str(), col::HILIGHT);
        s.text_mnemonic(font_ui, tx, ty, it.label.c_str(), tc);
        if (!it.shortcut.empty()) {
            int sw = font_ui.text_width(it.shortcut.c_str());
            s.text(font_ui, frame.w - 3 - 16 - sw, ty, it.shortcut.c_str(), tc);
        }
        if (it.sub) {
            int ax = frame.w - 12, ay = y + h / 2 - 4;
            for (int k = 0; k < 4; k++) s.vline(ax + k, ay + k, 8 - 2 * k, tc);
        }
    }
}

void PopupMenu::open_child(int i) {
    if (child) { wm::close_popups(child); child = nullptr; }
    if (i < 0 || !menu->items[i].sub || menu->items[i].disabled) return;
    target->on_menu_init(menu->items[i].sub);
    PopupMenu* c = new PopupMenu(menu->items[i].sub, target, false);
    c->parent = this;
    child = c;
    int x = frame.right() - 4, y = frame.y + item_y(i) - 3;
    if (x + c->frame.w > wm::screen_w()) x = frame.x - c->frame.w + 4;
    if (y + c->frame.h > wm::screen_h()) y = wm::screen_h() - c->frame.h;
    c->frame.x = max(0, x);
    c->frame.y = max(0, y);
    wm::open_popup(c);
}

void PopupMenu::activate_item(int i) {
    if (i < 0) return;
    const MenuItem& it = menu->items[i];
    if (it.disabled || it.separator) return;
    if (it.sub) { open_child(i); if (child) { child->hover = 0; child->invalidate(); } return; }
    int id = it.id;
    Window* t = target;
    wm::close_popups();
    if (t && !t->closing) t->on_command(id, 0);
}

void PopupMenu::on_mouse_move(const MouseEv& e) {
    int i = item_at(e.y);
    if (e.x < 0 || e.x >= frame.w) i = -1;
    if (i == hover) return;
    if (i < 0 && child) return; // mantiene el resaltado si hay submenú abierto
    hover = i;
    invalidate();
    if (i >= 0 && menu->items[i].sub) open_child(i);
    else if (child) { wm::close_popups(child); child = nullptr; }
}

void PopupMenu::on_mouse_down(const MouseEv& e) {
    int i = item_at(e.y);
    if (i >= 0 && menu->items[i].sub) open_child(i);
}

void PopupMenu::on_mouse_up(const MouseEv& e) {
    int i = item_at(e.y);
    if (i >= 0 && e.x >= 0 && e.x < frame.w && !menu->items[i].sub) activate_item(i);
}

bool PopupMenu::on_key(const InputEvent& e) {
    if (!e.pressed) return true;
    int n = (int)menu->items.size();
    auto step = [&](int dir) {
        for (int k = 0; k < n; k++) {
            hover = (hover + dir + n) % n;
            if (hover < 0) hover = 0;
            if (!menu->items[hover].separator) break;
        }
        invalidate();
    };
    switch (e.key) {
    case KEY_UP: if (hover < 0) hover = 0; step(-1); return true;
    case KEY_DOWN: step(1); return true;
    case KEY_ENTER: activate_item(hover); return true;
    case KEY_ESC:
        if (parent) { wm::close_popups(this); }
        else wm::close_popups();
        return true;
    case KEY_RIGHT:
        if (hover >= 0 && menu->items[hover].sub) { activate_item(hover); return true; }
        if (from_bar || (parent && parent->from_bar)) {
            PopupMenu* root = parent ? parent : this;
            Window* t = target;
            int next = (root->bar_index + 1) % (int)root->from_bar->menus.size();
            wm::close_popups();
            open_bar_menu(t, next);
        }
        return true;
    case KEY_LEFT:
        if (parent) { wm::close_popups(this); return true; }
        if (from_bar) {
            Window* t = target;
            int prev = (bar_index - 1 + (int)from_bar->menus.size()) % (int)from_bar->menus.size();
            wm::close_popups();
            open_bar_menu(t, prev);
        }
        return true;
    }
    // Letra de acceso
    if (e.ch) {
        for (int i = 0; i < n; i++) {
            const char* l = menu->items[i].label.c_str();
            for (int k = 0; l[k]; k++)
                if (l[k] == '&' && to_upper(l[k + 1]) == to_upper((char)e.ch)) { hover = i; activate_item(i); return true; }
        }
    }
    return true;
}

PopupMenu* show_menu(Menu* m, Window* target, int sx, int sy, bool large) {
    PopupMenu* p = new PopupMenu(m, target, large);
    if (sx + p->frame.w > wm::screen_w()) sx = wm::screen_w() - p->frame.w;
    if (sy + p->frame.h > wm::screen_h()) sy = max(0, sy - p->frame.h);
    p->frame.x = max(0, sx);
    p->frame.y = max(0, sy);
    wm::open_popup(p);
    return p;
}

#pragma once
#include "gui/wm.h"

class Menu;

struct MenuItem {
    String label;        // '&' marca la letra de acceso
    String shortcut;     // texto a la derecha (p. ej. "Ctrl+S")
    int id = 0;
    bool separator = false;
    bool checked = false;
    bool disabled = false;
    Menu* sub = nullptr;
    const IconPair* icon = nullptr;
};

class Menu {
public:
    Vec<MenuItem> items;
    Menu();
    ~Menu();
    Menu(const Menu&) = delete;
    Menu& operator=(const Menu&) = delete;
    void retranslate();
    static void retranslate_all();   // todos los menús que existen (también los contextuales)
    void clear();
    Menu& add(const char* label, int id, const char* shortcut = nullptr, const IconPair* icon = nullptr);
    Menu& submenu(const char* label, Menu* sub, const IconPair* icon = nullptr);
    Menu& separator();
    MenuItem* find(int id);
    void check(int id, bool on);
    void enable(int id, bool on);
};

class MenuBar {
public:
    Vec<String> titles;
    Vec<Menu*> menus;
    int open_index = -1;
    uint64_t closed_time = 0;
    int closed_index = -1;
    ~MenuBar();
    void retranslate();
    Menu* add(const char* title);
    int item_x(int i) const;
    int item_w(int i) const;
};

class PopupMenu : public Window {
public:
    Menu* menu;
    Window* target;
    PopupMenu* parent = nullptr;
    PopupMenu* child = nullptr;
    int hover = -1;
    bool large = false;           // estilo menú Inicio (iconos grandes y banda lateral)
    MenuBar* from_bar = nullptr;  // menú abierto desde una barra de menús
    int bar_index = -1;

    PopupMenu(Menu* m, Window* target, bool large);
    void on_destroyed() override;
    void on_paint(Surface& s) override;
    void on_mouse_move(const MouseEv& e) override;
    void on_mouse_up(const MouseEv& e) override;
    void on_mouse_down(const MouseEv& e) override;
    bool on_key(const InputEvent& e) override;

    int item_at(int y) const;
    int item_y(int i) const;
    int item_h(int i) const;
    void open_child(int i);
    void activate_item(int i);
};

// Muestra un menú contextual en la posición de pantalla indicada
PopupMenu* show_menu(Menu* m, Window* target, int sx, int sy, bool large = false);

// Integración con el gestor de ventanas
void attach_menubar(Window* w, MenuBar* mb);
void menubar_click(Window* w, int x, int y);
void menubar_hover(Window* w, int x);
bool menubar_mnemonic(Window* w, char c);
void paint_menubar(Window* w, Surface& s, int width);
void delete_menubar(MenuBar* mb);

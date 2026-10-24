#pragma once
#include "gui/wm.h"
#include "lib/i18n.h"

// Barra de desplazamiento integrada en otros controles
struct ScrollBar {
    bool vertical = true;
    Rect r;
    int pos = 0, page = 1, total = 1, line = 1;
    int drag = 0;        // 1 = arrastrando el cuadro
    int drag_off = 0;
    int pressed = 0;     // 1 flecha inicial, 2 flecha final

    int max_pos() const { return total > page ? total - page : 0; }
    void set(int p) { pos = clamp(p, 0, max_pos()); }
    bool needed() const { return total > page; }
    Rect thumb() const;
    void paint(Surface& s) const;
    // Devuelve true si el evento le pertenece; 'changed' indica si cambió pos
    bool mouse_down(int x, int y, bool& changed);
    void mouse_move(int x, int y, bool& changed);
    void mouse_up();
};

class Label : public Widget {
public:
    String text;
    const Font* font = &font_ui;
    uint32_t color = col::BLACK;
    int align = 0;          // 0 izquierda, 1 centro, 2 derecha
    bool sunken = false;    // estilo barra de estado
    uint32_t bg = 0xFFFFFFFF; // sin fondo
    Label(const Rect& r, const char* t, int align = 0);
    void set_text(const char* t) { text = t; invalidate(); }
    void paint(Surface& s) override;
    void retranslate() override { set_text(i18n::retranslate(text.c_str())); }
};

class Button : public Widget {
public:
    String text;
    const IconImg* icon = nullptr;
    bool is_default = false;
    bool flat = false;      // botón de barra de herramientas
    bool toggle = false;    // se queda pulsado (herramientas de Paint)
    bool down = false;      // estado de 'toggle'
    bool pressing = false, inside = false, hot = false;
    uint32_t swatch = 0xFFFFFFFF; // muestra de color (paleta de Paint)
    Button(const Rect& r, const char* text, int id);
    void retranslate() override { text = i18n::retranslate(text.c_str()); invalidate(); }
    void paint(Surface& s) override;
    void mouse_down(const MouseEv& e) override;
    void mouse_move(const MouseEv& e) override;
    void mouse_up(const MouseEv& e) override;
    bool key(const InputEvent& e) override;
};

class CheckBox : public Widget {
public:
    String text;
    bool checked = false;
    bool radio = false;
    CheckBox(const Rect& r, const char* text, int id, bool checked = false);
    void retranslate() override { text = i18n::retranslate(text.c_str()); invalidate(); }
    void paint(Surface& s) override;
    void mouse_up(const MouseEv& e) override;
    bool key(const InputEvent& e) override;
};

// Campo de texto de una línea. notify(1) al pulsar Intro, notify(2) al cambiar.
class TextBox : public Widget {
public:
    String text;
    int cpos = 0, anchor = 0, scroll = 0;
    bool caret_on = true;
    bool selecting = false;
    bool read_only = false;
    TextBox(const Rect& r, const char* text = "", int id = 0);
    void set_text(const char* t);
    void select_all();
    void paint(Surface& s) override;
    void mouse_down(const MouseEv& e) override;
    void mouse_move(const MouseEv& e) override;
    void mouse_up(const MouseEv& e) override;
    bool key(const InputEvent& e) override;
    void focus_changed(bool f) override;
    void tick() override;
    Cursor cursor(int, int) override { return CURSOR_IBEAM; }

private:
    int pos_from_x(int x);
    void delete_selection();
    void ensure_visible();
};

// Editor de texto multilínea con barras de desplazamiento (Bloc de notas)
class TextArea : public Widget {
public:
    Vec<String> lines;
    int cy = 0, cx = 0;          // cursor (línea, columna)
    int ay = 0, ax = 0;          // ancla de la selección
    int top = 0, left = 0;       // desplazamiento (líneas, píxeles)
    bool caret_on = true;
    bool selecting = false;
    bool modified = false;
    bool read_only = false;
    const Font* font = &font_mono;
    uint32_t fg = col::BLACK, bgc = col::WHITE;
    ScrollBar vs, hs;
    int sb_capture = 0;

    TextArea(const Rect& r, int id = 0);
    void set_text(const char* t, size_t len);
    String get_text() const;      // líneas unidas con \r\n
    size_t text_size() const;
    void insert(const char* s);   // en la posición del cursor (sustituye la selección)
    void select_all();
    bool has_selection() const { return cy != ay || cx != ax; }
    String selected_text() const;
    void delete_selection();
    void copy();
    void cut();
    void paste();
    bool find(const char* needle, bool match_case, bool down);
    void goto_line(int line);
    void paint(Surface& s) override;
    void mouse_down(const MouseEv& e) override;
    void mouse_move(const MouseEv& e) override;
    void mouse_up(const MouseEv& e) override;
    void mouse_wheel(const MouseEv& e) override;
    bool key(const InputEvent& e) override;
    void focus_changed(bool f) override;
    void tick() override;
    Cursor cursor(int x, int y) override;

private:
    Rect text_rect() const;
    int line_h() const { return font->height; }
    int col_to_x(int line, int col) const;
    int x_to_col(int line, int x) const;
    void layout_scrollbars();
    void ensure_visible();
    void move_cursor(int line, int col, bool extend);
    void order_sel(int& y0, int& x0, int& y1, int& x1) const;
    void changed();
};

struct ListItem {
    String text;
    Vec<String> sub;           // columnas adicionales (vista Detalles)
    const IconPair* icon = nullptr;
    bool selected = false;
    int64_t data = 0;
    String path;
};

struct ListColumn {
    String title;
    int width;
    bool right = false;
};

enum ListNotify { LV_ACTIVATE = 1, LV_SELCHANGE, LV_CONTEXT, LV_DELETE, LV_RENAME, LV_BACK, LV_COLUMN, LV_KEY };

class ListView : public Widget {
public:
    enum Mode { ICONS, DETAILS, LIST } mode = ICONS;
    Vec<ListItem> items;
    Vec<ListColumn> columns;
    int focus_item = -1;
    int anchor_item = -1;
    int scroll = 0;              // en píxeles
    ScrollBar vs;
    int sb_capture = 0;
    int clicked_column = -1;
    int ctx_x = 0, ctx_y = 0;    // posición del clic derecho (pantalla)
    int last_key = 0;
    bool icon_white_text = false;

    ListView(const Rect& r, int id);
    void clear();
    ListItem& add(const char* text, const IconPair* icon, int64_t data = 0);
    int selected() const;        // primer elemento seleccionado o -1
    int selected_count() const;
    void select(int i, bool only = true);
    void ensure_visible(int i);
    void paint(Surface& s) override;
    void retranslate() override {
        for (auto& c : columns) c.title = i18n::retranslate(c.title.c_str());
        // elementos que son textos de la interfaz (colores, distribuciones de teclado...);
        // las listas de archivos se vuelven a leer en su ventana
        for (auto& it : items) {
            it.text = i18n::retranslate(it.text.c_str());
            for (auto& s : it.sub) s = i18n::retranslate(s.c_str());
        }
        invalidate();
    }
    void mouse_down(const MouseEv& e) override;
    void mouse_up(const MouseEv& e) override;
    void mouse_move(const MouseEv& e) override;
    void mouse_wheel(const MouseEv& e) override;
    bool key(const InputEvent& e) override;
    int item_at(int x, int y) const;

private:
    int cell_w() const { return mode == ICONS ? 76 : (mode == LIST ? 180 : r.w); }
    int cell_h() const { return mode == ICONS ? 68 : 18; }
    int header_h() const { return mode == DETAILS ? 18 : 0; }
    int per_row() const;
    Rect item_rect(int i) const;
    int content_h() const;
    void layout();
};

// Barra de progreso con bloques azules
class ProgressBar : public Widget {
public:
    int value = 0, maximum = 100;
    explicit ProgressBar(const Rect& r) { this->r = r; }
    void set(int v) { value = v; invalidate(); }
    void paint(Surface& s) override;
};

// Control deslizante (trackbar). notify(1) cada vez que cambia el valor.
// En vertical, el valor máximo queda arriba (como el control de volumen).
class TrackBar : public Widget {
public:
    int minimum = 0, maximum = 100, value = 0;
    int page = 10;          // salto con RePág/AvPág y al hacer clic en el canal
    int tick_every = 10;    // marcas (0 = ninguna)
    bool vertical = false;
    bool dragging = false;
    TrackBar(const Rect& r, int id, bool vertical = false);
    void set(int v);
    void paint(Surface& s) override;
    void mouse_down(const MouseEv& e) override;
    void mouse_move(const MouseEv& e) override;
    void mouse_up(const MouseEv& e) override;
    void mouse_wheel(const MouseEv& e) override;
    bool key(const InputEvent& e) override;

private:
    int track_len() const;
    int value_to_pos(int v) const;
    int pos_to_value(int p) const;
    void change(int v);
};

// Utilidades de dibujo compartidas
int label_width_mn(const char* s);   // anchura sin contar los '&'
void draw_icon_label(Surface& s, const Rect& cell, const IconPair* icon, const char* text, bool selected,
                     bool focused, bool white_text, uint32_t bg);
void draw_checker(Surface& s, const Rect& r);
void draw_arrow(Surface& s, int cx, int cy, int dir, uint32_t c); // 0 arriba 1 abajo 2 izq 3 der

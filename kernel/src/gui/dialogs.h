#pragma once
#include "gui/widgets.h"

enum DlgResult { ID_NONE = 0, ID_OK = 1, ID_CANCEL, ID_YES, ID_NO };
enum MsgButtons { MB_OK = 0, MB_OKCANCEL, MB_YESNO, MB_YESNOCANCEL };

// Cuadro de mensaje modal. 'icon' puede ser &icon_info, &icon_warning, &icon_error, &icon_question
int msgbox(Window* owner, const char* title, const char* text, int buttons = MB_OK,
           const IconPair* icon = &icon_info);

// Igual, pero sin esperar: devuelve la ventana y, al cerrarse, escribe el botón en *result
Window* msgbox_async(Window* owner, const char* title, const char* text, int buttons, const IconPair* icon,
                     volatile int* result);

// Pide un texto. Devuelve true si se aceptó.
bool input_box(Window* owner, const char* title, const char* prompt, String& value);

// Diálogo Abrir / Guardar como. 'filter' es una extensión ("TXT", "BMP") o nullptr para todos.
bool file_dialog(Window* owner, bool save, const char* title, const char* filter, String& path);

// Ventana de diálogo base con botones estándar
class Dialog : public Window {
public:
    Dialog(const char* title, int w, int h);
    void on_command(int id, int code) override;
    bool on_key(const InputEvent& e) override;
    virtual bool on_ok() { return true; }   // validar antes de cerrar con Aceptar
    void end(int result);
    Button* default_btn = nullptr;
};

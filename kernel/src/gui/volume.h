#pragma once
#include "gfx/gfx.h"

// Control de volumen: icono del altavoz en la barra de tareas, ventana emergente con el
// deslizador y teclas multimedia (subir, bajar y silenciar).
namespace volume_ui {
void init();
void change(int delta);          // sube o baja el volumen general y muestra el control
void toggle_mute();
void show_popup(bool from_key);  // junto al altavoz de la barra de tareas
void draw_tray_icon(Surface& s, int x, int y);
void tick();                     // cada segundo: guarda los cambios cuando el usuario termina
}

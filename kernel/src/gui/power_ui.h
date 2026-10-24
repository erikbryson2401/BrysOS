#pragma once
#include "gfx/gfx.h"

// Medidor de energía: icono de la batería en la barra de tareas, ventana con la carga y el
// brillo de la pantalla, y avisos de batería baja.
namespace power_ui {
void init();                      // aplica el brillo guardado
bool tray_visible();              // hay batería: se muestra el icono
void draw_tray_icon(Surface& s, int x, int y);
void show_popup();
void tick();                      // cada segundo (desde la barra de tareas)
}

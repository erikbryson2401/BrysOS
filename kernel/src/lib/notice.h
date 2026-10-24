#pragma once
#include "lib/vec.h"

// Avisos para el usuario generados fuera de la interfaz (controladores, montaje de unidades).
// Se guardan en una cola y la barra de tareas los muestra en un cuadro de mensaje.
namespace notice {
void post(const char* title, const char* text, bool error = false);
bool take(String& title, String& text, bool& error);
}

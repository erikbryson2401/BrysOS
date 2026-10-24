#pragma once

// Presentación de arranque (logo + sonido) y pantalla de carga con el progreso real
namespace bootsplash {
// Muestra el logo con fundido de entrada, reproduce el sonido de inicio y desvanece el
// logo al terminar el audio.
void intro();
void loading_begin();
// Paso de inicialización en curso (texto ya traducido) y porcentaje completado
void loading_step(const char* text, int percent);
bool loading_active();
// Error que impide arrancar: muestra el mensaje y reinicia al pulsar una tecla
[[noreturn]] void fatal(const char* title, const char* text);
}

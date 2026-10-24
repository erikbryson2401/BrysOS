#pragma once

// Consola de texto de arranque dibujada directamente en el framebuffer
void bootscreen_init();
void bootscreen_log(const char* s);
void bootscreen_disable();
// Pantalla azul de error fatal
void bluescreen(const char* title, const char* msg);

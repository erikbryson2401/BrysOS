#pragma once

// Lanzadores de las aplicaciones del sistema
namespace apps {
void notepad(const char* path = nullptr);
void paint(const char* path = nullptr);
void calc();
void cmd(const char* start_dir = nullptr);
void minesweeper();
void explorer(const char* path = nullptr);
void find(const char* start_dir = nullptr);
void help();
// Créditos y licencias (first_boot: modal, sólo se cierra con Aceptar)
void credits(bool first_boot = false);
void run_dialog();
void shutdown_dialog();
void control_panel();
void properties(const char* path);
void installer();
void datetime_dialog();
// Actualizador: aplica un paquete de actualización .bsi
void updater(const char* path);
// Visor de imágenes (images.bsl) y reproductor de sonido (sonido.bsl)
void viewer(const char* path = nullptr);
// Programas .run: arranca uno y diálogo "Cerrar programa" (Ctrl+Alt+Supr)
bool run_program(const char* path, const char* args = nullptr);
void close_program_dialog();
// Fondo de escritorio (con una imagen ya elegida, p. ej. desde el Visor)
void wallpaper_dialog(const char* path = nullptr);
void player(const char* path = nullptr);
bool image_file(const char* ext);   // extensión que images.bsl sabe abrir
bool sound_file(const char* ext);   // extensión que sonido.bsl sabe abrir

// Abre un archivo con la aplicación asociada a su extensión
bool open_file(const char* path);
// Ejecuta una orden de "Ejecutar..." (nombre de programa o ruta)
bool run_command(const char* cmdline);
}

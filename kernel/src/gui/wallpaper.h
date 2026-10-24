#pragma once
#include "gfx/gfx.h"
#include "lib/vec.h"

// Fondo de escritorio: una imagen (cualquier formato de images.bsl) centrada, en mosaico,
// expandida o ajustada a la pantalla. Se guarda reducida al tamaño de la pantalla en
// C:\BrysOS\fondo.bmp, así no depende del archivo original (que puede estar en un pendrive).
namespace wallpaper {
enum Mode { CENTER, TILE, STRETCH, FIT };

// Prepara 'path' como fondo (lo decodifica, lo reduce y lo guarda). 'err' explica el fallo.
bool set(const char* path, int mode, String& err);
void set_mode(int mode);
void clear();                       // sin fondo: sólo el color
bool load();                        // al arrancar, según la configuración
bool active();
int mode();
void paint(Surface& s, uint32_t bg); // dibuja el escritorio entero (color + imagen)
// Para la vista previa: decodifica una imagen (reducida al tamaño de la pantalla) y compone
// en 'out' (cualquier tamaño) cómo quedaría el escritorio con ella
bool load_image(const char* path, Surface& out, String& err);
void compose_preview(Surface& out, const Surface& img, int mode, uint32_t bg);
const Surface* current();           // el fondo actual (nullptr si no hay)
}

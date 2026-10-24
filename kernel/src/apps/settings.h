#pragma once
#include <stdint.h>

// Configuración persistente en C:\BrysOS\config.ini
namespace settings {
extern uint32_t desktop_color;
extern int mouse_speed;        // 1..4
extern char language[8];       // "es" / "en"
extern char keyboard[16];      // "es", "latam", "us"
extern int utc_offset;         // minutos respecto a UTC
extern bool rtc_utc;           // el reloj del equipo guarda la hora UTC
extern bool intro_always;      // logo y sonido de inicio en cada arranque
extern int volume;             // volumen general 0..100
extern bool muted;             // silencio
extern int intro_volume;       // volumen del sonido de inicio 0..100 (sobre el general)
extern bool wallpaper;         // hay fondo de escritorio (C:\\BrysOS\\fondo.bmp)
extern int wallpaper_mode;     // 0 centrado, 1 mosaico, 2 expandido, 3 ajustado
extern int brightness;         // brillo de la pantalla en % (-1: el del firmware)
extern bool credits_hidden;    // no mostrar "Créditos y licencias" al iniciar
void load();
bool save();
void apply();
// Texto del archivo de configuración con los valores actuales
void serialize(char* out, int cap);
// Activa o quita la presentación (logo + sonido) del siguiente arranque en grub.cfg
bool set_boot_intro(bool intro);
// Vuelve a escribir el idioma y el volumen en grub.cfg sin tocar la presentación
bool update_boot_line();
}

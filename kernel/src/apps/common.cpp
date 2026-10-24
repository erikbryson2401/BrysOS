#include "apps/common.h"

namespace fileclip {
Vec<String> paths;
bool cut = false;
}

static bool is_text_ext(const String& e) {
    const char* exts[] = {"TXT", "INI", "LOG", "CFG", "BAT", "C", "CPP", "H", "HPP", "MD", "CSV", "ASM", "PY", "JSON", "XML", "HTM", "HTML", "CSS", "JS", "INF", nullptr};
    for (int i = 0; exts[i]; i++) if (e == exts[i]) return true;
    return false;
}

const IconPair* icon_for(const char* name, bool dir) {
    if (dir) return &icon_folder;
    String e = vfs::extension(name);
    if (is_text_ext(e)) return &icon_file_text;
    if (e == "BMP") return &icon_file_image;
    if (e == "BSI") return &icon_package;
    if (e == "BSL") return &icon_settings;
    if (e == "RUN") return &icon_app;
    if (apps::image_file(e.c_str())) return &icon_file_image;
    if (apps::sound_file(e.c_str())) return &icon_sound;
    return &icon_file;
}

String type_name(const char* name, bool dir) {
    if (dir) return T("Carpeta de archivos");
    String e = vfs::extension(name);
    if (e == "TXT") return T("Documento de texto");
    if (e == "BMP") return T("Imagen de mapa de bits");
    if (e == "BSI") return T("Paquete de actualización de BrysOS");
    if (e == "BSL") return T("Biblioteca del sistema");
    if (e == "RUN") return T("Programa");
    if (e == "PNG") return T("Imagen PNG");
    if (e == "JPG" || e == "JPEG" || e == "JPE" || e == "JFIF") return T("Imagen JPEG");
    if (e == "GIF") return T("Imagen GIF");
    if (e == "WAV") return T("Sonido WAV");
    if (e == "INI" || e == "CFG" || e == "INF") return T("Opciones de configuración");
    if (e == "LOG") return T("Registro");
    if (e == "BAT") return T("Archivo por lotes");
    if (is_text_ext(e)) return String::format(T("Archivo %s"), e.c_str());
    if (e.empty()) return T("Archivo");
    return String::format(T("Archivo %s"), e.c_str());
}

String format_date(const DateTime& t) {
    return String::format(T("%02d/%02d/%04d %d:%02d"), t.day, t.month, t.year, t.hour, t.minute);
}

const TimeZone time_zones[] = {
    {-720, "(UTC-12:00) Línea internacional de cambio de fecha"},
    {-600, "(UTC-10:00) Hawái"},
    {-540, "(UTC-09:00) Alaska"},
    {-480, "(UTC-08:00) Hora del Pacífico (EE. UU. y Canadá)"},
    {-420, "(UTC-07:00) Hora de las Montañas Rocosas"},
    {-360, "(UTC-06:00) Ciudad de México, Centroamérica"},
    {-300, "(UTC-05:00) Bogotá, Lima, Quito, Nueva York"},
    {-240, "(UTC-04:00) Caracas, La Paz, Santiago"},
    {-180, "(UTC-03:00) Buenos Aires, Montevideo, São Paulo"},
    {-120, "(UTC-02:00) Atlántico central"},
    {-60, "(UTC-01:00) Azores, Cabo Verde"},
    {0, "(UTC+00:00) Londres, Lisboa, Canarias"},
    {60, "(UTC+01:00) Madrid, París, Berlín, Roma"},
    {120, "(UTC+02:00) Atenas, Helsinki, El Cairo"},
    {180, "(UTC+03:00) Moscú, Estambul"},
    {240, "(UTC+04:00) Dubái"},
    {300, "(UTC+05:00) Karachi"},
    {330, "(UTC+05:30) Nueva Delhi"},
    {360, "(UTC+06:00) Daca"},
    {420, "(UTC+07:00) Bangkok, Yakarta"},
    {480, "(UTC+08:00) Pekín, Singapur"},
    {540, "(UTC+09:00) Tokio, Seúl"},
    {600, "(UTC+10:00) Sídney"},
    {720, "(UTC+12:00) Auckland"},
};
const int time_zone_count = sizeof(time_zones) / sizeof(time_zones[0]);

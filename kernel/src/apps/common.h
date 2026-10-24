#pragma once
#include "gui/widgets.h"
#include "gui/menu.h"
#include "gui/dialogs.h"
#include "fs/vfs.h"
#include "apps/apps.h"
#include "lib/i18n.h"

// Icono según el tipo de archivo
const IconPair* icon_for(const char* name, bool dir);
// Descripción del tipo ("Documento de texto", "Imagen de mapa de bits"...)
String type_name(const char* name, bool dir);
// Fecha "18/10/2026 16:05"
String format_date(const DateTime& t);

// Portapapeles de archivos del Explorador
namespace fileclip {
extern Vec<String> paths;
extern bool cut;
}

// Zonas horarias (desplazamiento en minutos y nombre en español; usar T())
struct TimeZone { int offset; const char* name; };
extern const TimeZone time_zones[];
extern const int time_zone_count;

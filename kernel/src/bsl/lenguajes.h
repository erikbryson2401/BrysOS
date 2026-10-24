#pragma once
// API de lenguajes.bsl: los textos de la interfaz en cada idioma.
// Los textos del sistema se escriben en español; esa cadena es la clave de la traducción.
#include <stdint.h>

constexpr uint32_t LANG_API = 1;

struct LangApi {
    int (*count)();                                   // idiomas disponibles (el español incluido)
    const char* (*code)(int i);                       // "es", "en"
    const char* (*name)(int i);                       // "Español", "English"
    // Texto en el idioma pedido (el mismo texto si no hay traducción)
    const char* (*translate)(const char* lang, const char* es);
    // Clave (texto en español) de un texto en cualquier idioma, o nullptr si no es de la interfaz
    const char* (*key_of)(const char* text);
};

#pragma once

// Idioma de la interfaz. Los textos del código están en español; T() pide a
// lenguajes.bsl la traducción al idioma activo (o devuelve el mismo texto si no la hay
// o si la biblioteca no está cargada).
enum class Lang { ES, EN };

namespace i18n {
void set_language(Lang l);
Lang language();
const char* code();          // "es" / "en"
Lang from_code(const char* c);
// Conecta con lenguajes.bsl (después de cargar las bibliotecas)
void attach();
bool available();            // lenguajes.bsl cargada
// Traduce al idioma activo un texto que ya estaba traducido a otro idioma (ventanas abiertas)
const char* retranslate(const char* text);
}

const char* T(const char* es);

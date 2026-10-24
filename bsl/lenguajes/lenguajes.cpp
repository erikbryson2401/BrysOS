// lenguajes.bsl: traducciones de la interfaz. Todos los textos que se ven en las ventanas
// pasan por aquí (función T() del núcleo); al cambiar de idioma, el sistema vuelve a
// traducir también las ventanas abiertas buscando la clave de cada texto con key_of().
#include "bsl/bsl.h"
#include "bsl/lenguajes.h"
#include "lib/string.h"
#include "mm/memory.h"

struct Translation {
    const char* es;
    const char* other;
};
// Tablas generadas por tools/gen_translations.py
extern const Translation translations_en[];
extern const int translations_en_count;

namespace {
struct Language {
    const char* code;
    const char* name;
    const Translation* table;
    int count;
};
const Language langs[] = {
    {"es", "Español", nullptr, 0},
    {"en", "English", translations_en, 0},
};
constexpr int NLANG = sizeof(langs) / sizeof(langs[0]);

// Tablas hash (direccionamiento abierto): de la clave a la traducción y de la traducción
// a la clave, por idioma
constexpr int TABLE = 8192;
const Translation** fwd[NLANG];
const Translation** rev[NLANG];

uint32_t hash(const char* s) {
    uint32_t h = 2166136261u;
    for (; *s; s++) h = (h ^ (uint8_t)*s) * 16777619u;
    return h;
}

int count_of(int l) { return l == 1 ? translations_en_count : 0; }

void insert(const Translation** t, const char* key, const Translation* v) {
    uint32_t h = hash(key) % TABLE;
    while (t[h]) h = (h + 1) % TABLE;
    t[h] = v;
}

bool init() {
    for (int l = 1; l < NLANG; l++) {
        fwd[l] = (const Translation**)mm::kcalloc(sizeof(void*) * TABLE);
        rev[l] = (const Translation**)mm::kcalloc(sizeof(void*) * TABLE);
        if (!fwd[l] || !rev[l]) return false;
        for (int i = 0; i < count_of(l); i++) {
            insert(fwd[l], langs[l].table[i].es, &langs[l].table[i]);
            insert(rev[l], langs[l].table[i].other, &langs[l].table[i]);
        }
    }
    return true;
}

int index_of(const char* code) {
    for (int l = 0; l < NLANG; l++)
        if (code && !stricmp(code, langs[l].code)) return l;
    return 0;
}

const Translation* lookup(const Translation** t, const char* key, bool by_es) {
    uint32_t h = hash(key) % TABLE;
    while (t[h]) {
        if (!strcmp(by_es ? t[h]->es : t[h]->other, key)) return t[h];
        h = (h + 1) % TABLE;
    }
    return nullptr;
}

int count() { return NLANG; }
const char* code(int i) { return i >= 0 && i < NLANG ? langs[i].code : "es"; }
const char* name(int i) { return i >= 0 && i < NLANG ? langs[i].name : "Español"; }

const char* translate(const char* lang, const char* es) {
    int l = index_of(lang);
    if (l == 0 || !es || !es[0]) return es;
    const Translation* t = lookup(fwd[l], es, true);
    return t ? t->other : es;
}

const char* key_of(const char* text) {
    if (!text || !text[0]) return nullptr;
    for (int l = 1; l < NLANG; l++) {
        if (const Translation* t = lookup(fwd[l], text, true)) return t->es;    // ya es la clave
        if (const Translation* t = lookup(rev[l], text, false)) return t->es;   // es una traducción
    }
    return nullptr;
}

const LangApi api_v1 = {count, code, name, translate, key_of};
const void* get_api(uint32_t v) { return v == LANG_API ? &api_v1 : nullptr; }
}

BSL_DEFINE("lenguajes", "1.0", "Textos de la interfaz en español e inglés", init, get_api);

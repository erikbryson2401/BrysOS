#include "lib/i18n.h"
#include "lib/string.h"
#include "bsl/loader.h"
#include "bsl/lenguajes.h"

namespace {
Lang current = Lang::ES;
const LangApi* api = nullptr;
}

void i18n::set_language(Lang l) { current = l; }
Lang i18n::language() { return current; }
const char* i18n::code() { return current == Lang::EN ? "en" : "es"; }
Lang i18n::from_code(const char* c) { return (c && (c[0] == 'e' || c[0] == 'E') && (c[1] == 'n' || c[1] == 'N')) ? Lang::EN : Lang::ES; }
void i18n::attach() { api = bsl::get<LangApi>("lenguajes", LANG_API); }
bool i18n::available() { return api != nullptr; }

const char* T(const char* es) {
    if (current == Lang::ES || !es || !es[0] || !api) return es;
    return api->translate(i18n::code(), es);
}

const char* i18n::retranslate(const char* text) {
    if (!api) return text;
    const char* key = api->key_of(text);
    return key ? T(key) : text;
}

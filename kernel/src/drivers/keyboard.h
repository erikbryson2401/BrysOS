#pragma once
#include <stdint.h>

// Decodificador de teclado común (PS/2 y USB): recibe códigos del conjunto 1
// y genera eventos según la distribución activa.
namespace keyboard {
struct LayoutInfo {
    const char* id;     // "es", "latam", "us"
    const char* name;   // nombre visible (en español; usar T())
};
int layout_count();
const LayoutInfo& layout(int i);
bool set_layout(const char* id);
const char* layout_id();

void scancode(uint8_t b);
void set_led_hook(void (*fn)(bool caps));   // se pueden registrar varios (PS/2 y USB)
}

#pragma once
#include "gfx/gfx.h"
#include "lib/vec.h"

// Lee un BMP (1, 4, 8, 24 o 32 bits sin compresión) en una superficie nueva
bool bmp_decode(const uint8_t* data, uint32_t size, Surface& out, String& err);
// Codifica una superficie como BMP de 24 bits
Vec<uint8_t> bmp_encode(const Surface& s);

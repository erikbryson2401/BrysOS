#pragma once
#include <stddef.h>
#include <stdint.h>

// Números aleatorios para criptografía (claves de TLS, getrandom, /dev/urandom). El Core 2 Duo
// del HP dv5 no tiene RDRAND, así que, como Linux, se recoge entropía del momento exacto (TSC)
// en que llega cada interrupción y se usa para sembrar un generador ChaCha20.
namespace random {
void add_entropy(uint64_t v);           // muy barata: se llama en cada interrupción
void bytes(void* out, size_t n);
uint64_t u64();
}

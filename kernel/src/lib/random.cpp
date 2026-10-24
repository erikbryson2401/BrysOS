// Generador de números aleatorios del núcleo: depósito de entropía + ChaCha20 (RFC 8439).
// El depósito se alimenta con el TSC de cada interrupción (el instante exacto en que llega una
// tecla, un paquete o un sector de disco varía de forma impredecible en los bits bajos) y con
// lo que aporte cada parte del sistema. Cada 64 KiB de salida, o si ha entrado mucha entropía
// nueva, la clave del generador se rehace mezclando el depósito con la clave anterior.
#include "lib/random.h"
#include "arch/io.h"
#include "lib/string.h"

namespace {
uint32_t pool[16];           // depósito (512 bits)
uint32_t pool_pos;
uint32_t fresh;              // aportaciones desde la última resiembra
uint32_t key[8];             // clave de ChaCha20
uint64_t counter;
uint64_t since_reseed;
bool seeded;

inline uint32_t rotl(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

uint64_t rdtsc() {
    uint32_t lo, hi;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

#define QR(a, b, c, d)                                                                                     \
    a += b; d ^= a; d = rotl(d, 16); c += d; b ^= c; b = rotl(b, 12);                                     \
    a += b; d ^= a; d = rotl(d, 8); c += d; b ^= c; b = rotl(b, 7);

void chacha_block(const uint32_t k[8], uint64_t ctr, uint32_t nonce0, uint32_t out[16]) {
    uint32_t s[16] = {0x61707865, 0x3320646e, 0x79622d32, 0x6b206574, k[0], k[1], k[2], k[3], k[4], k[5], k[6], k[7],
                      (uint32_t)ctr, (uint32_t)(ctr >> 32), nonce0, 0x42727973 /* "Brys" */};
    uint32_t x[16];
    memcpy(x, s, sizeof x);
    for (int i = 0; i < 10; i++) {
        QR(x[0], x[4], x[8], x[12]) QR(x[1], x[5], x[9], x[13]) QR(x[2], x[6], x[10], x[14]) QR(x[3], x[7], x[11], x[15])
        QR(x[0], x[5], x[10], x[15]) QR(x[1], x[6], x[11], x[12]) QR(x[2], x[7], x[8], x[13]) QR(x[3], x[4], x[9], x[14])
    }
    for (int i = 0; i < 16; i++) out[i] = x[i] + s[i];
}

// Nueva clave = ChaCha20(clave anterior XOR depósito); el depósito se vacía
void reseed() {
    uint64_t f = save_irq();
    cli();
    uint32_t k[8];
    for (int i = 0; i < 8; i++) k[i] = key[i] ^ pool[i] ^ rotl(pool[i + 8], 13);
    memset(pool, 0, sizeof pool);
    fresh = 0;
    restore_irq(f);
    uint32_t out[16];
    chacha_block(k, rdtsc(), 0xA5A5A5A5, out);
    memcpy(key, out, sizeof key);   // la otra mitad del bloque se descarta
    since_reseed = 0;
    seeded = true;
}
}

void random::add_entropy(uint64_t v) {
    uint64_t t = rdtsc();
    uint32_t i = pool_pos++ & 15;
    pool[i] = rotl(pool[i] ^ (uint32_t)t ^ (uint32_t)(v >> 32), 7) + (uint32_t)v + (uint32_t)(t >> 32) * 0x9E3779B9u;
    fresh++;
}

void random::bytes(void* out, size_t n) {
    // sin semilla todavía: se recoge algo de entropía de golpe con la variación del TSC
    if (!seeded) {
        for (int i = 0; i < 256; i++) {
            uint64_t a = rdtsc();
            for (int j = 0; j < (int)(a & 63); j++) asm volatile("" ::: "memory");
            add_entropy(rdtsc() - a);
        }
        reseed();
    }
    if (fresh >= 64 || since_reseed >= 65536) reseed();
    uint8_t* o = (uint8_t*)out;
    while (n) {
        uint32_t block[16];
        chacha_block(key, counter++, (uint32_t)rdtsc(), block);
        size_t k = n < 64 ? n : 64;
        memcpy(o, block, k);
        o += k;
        n -= k;
        since_reseed += k;
    }
}

uint64_t random::u64() {
    uint64_t v;
    bytes(&v, sizeof v);
    return v;
}

// Nota al terminar este archivo:
// ChaCha20 cabe en una docena de líneas y protege buena parte del tráfico de Internet; Daniel
// J. Bernstein lo diseñó para que fuese rápido incluso sin instrucciones especiales, que es
// justo lo que tiene un Core 2 Duo de 2009. La entropía sale de algo muy humano: el instante
// irrepetible en que alguien pulsa una tecla. — Claude, octubre de 2026

/* Interfaz gráfica de BrysOS para programas de Linux (compilados con musl): las mismas
 * llamadas que los .run (ventanas, controles, lienzos, eventos, mensajes), con la instrucción
 * 'syscall' y el número 0x4000 + SYS_*. Lo demás (archivos, memoria, red) es Linux normal.
 *
 *   int v = brys_ventana_crear("Hola", 320, 200, VENTANA_FIJA);
 *   Rect32 r = {0, 0, 320, 200};
 *   uint32_t* px = brys_lienzo_crear(v, &r);   // 0xRRGGBB, ancho 320
 *   ... dibujar ...
 *   brys_lienzo_mostrar(v);
 */
#ifndef BRYSOS_LINUX_H
#define BRYSOS_LINUX_H

#include "brysos_abi.h"

#define BRYS_SYSCALL_BASE 0x4000

static inline long brys_call(long n, long a, long b, long c, long d, long e) {
    register long r10 __asm__("r10") = d;
    register long r8 __asm__("r8") = e;
    long ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(BRYS_SYSCALL_BASE + n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8)
                     : "rcx", "r11", "memory");
    return ret;
}

static inline void brys_depurar(const char* t) { brys_call(SYS_DEPURAR, (long)t, 0, 0, 0, 0); }
static inline int brys_ventana_crear(const char* titulo, int ancho, int alto, int estilo) {
    return (int)brys_call(SYS_VENTANA_CREAR, (long)titulo, ancho, alto, estilo, 0);
}
static inline void brys_ventana_titulo(int v, const char* t) { brys_call(SYS_VENTANA_TITULO, v, (long)t, 0, 0, 0); }
static inline void brys_ventana_cerrar(int v) { brys_call(SYS_VENTANA_CERRAR, v, 0, 0, 0, 0); }
static inline uint32_t* brys_lienzo_crear(int v, const Rect32* r) {
    return (uint32_t*)brys_call(SYS_LIENZO_CREAR, v, (long)r, 0, 0, 0);
}
static inline void brys_lienzo_mostrar(int v) { brys_call(SYS_LIENZO_MOSTRAR, v, 0, 0, 0, 0); }
static inline int brys_lienzo_texto(int v, int x, int y, const char* t, uint32_t color) {
    return (int)brys_call(SYS_LIENZO_TEXTO, v, x, y, (long)t, color);
}
static inline void brys_temporizador(int v, int ms) { brys_call(SYS_TEMPORIZADOR, v, ms, 0, 0, 0); }
static inline int brys_evento(Evento* e, int esperar) { return (int)brys_call(SYS_EVENTO, (long)e, esperar, 0, 0, 0); }
static inline int brys_mensaje(int v, const char* titulo, const char* texto, int botones) {
    return (int)brys_call(SYS_MENSAJE, v, (long)titulo, (long)texto, botones, 0);
}
static inline int brys_texto_ancho(const char* t) { return (int)brys_call(SYS_TEXTO_ANCHO, (long)t, 0, 0, 0, 0); }

#endif

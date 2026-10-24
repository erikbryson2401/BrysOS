/* BrysOS SDK: programas .run
 *
 * Un .run es un programa de 64 bits que BrysOS ejecuta aparte del núcleo (en modo usuario):
 * si falla o se queda colgado, sólo se cierra él. Se escribe en C con esta cabecera y se
 * compila con el Makefile de BrysOS (los .c de programas/<nombre>/ -> build/run/<nombre>.run).
 *
 *     #include <brysos.h>
 *     int main(void) {
 *         int v = ventana_crear("Hola", 300, 120, VENTANA_FIJA);
 *         etiqueta(v, 12, 12, 270, 16, "Hola desde un programa .run");
 *         Evento e;
 *         while (evento_esperar(&e) && e.tipo != EV_CERRAR) {}
 *         return 0;
 *     }
 */
#ifndef BRYSOS_H
#define BRYSOS_H

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include "brysos_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- Sistema */
__attribute__((noreturn)) void salir(int codigo);
void depurar(const char* texto);            /* al registro del sistema (orden REGISTRO) */
uint64_t milisegundos(void);                /* desde que arrancó el equipo */
void dormir(uint32_t ms);
const char* argumentos(void);               /* línea de órdenes (p. ej. el archivo a abrir) */

/* --- Memoria y cadenas */
void* malloc(size_t n);
void* calloc(size_t n, size_t tam);
void* realloc(void* p, size_t n);
void free(void* p);
void* memcpy(void* d, const void* s, size_t n);
void* memmove(void* d, const void* s, size_t n);
void* memset(void* d, int c, size_t n);
int memcmp(const void* a, const void* b, size_t n);
size_t strlen(const char* s);
char* strcpy(char* d, const char* s);
char* strncpy(char* d, const char* s, size_t n);
char* strcat(char* d, const char* s);
int strcmp(const char* a, const char* b);
int strncmp(const char* a, const char* b, size_t n);
int numero(const char* s);                  /* como atoi */
/* Como snprintf: %d %i %u %x %X %c %s %%, con l/ll, ancho, relleno con 0 y '-' para alinear a la izquierda */
int formatear(char* buf, size_t max, const char* fmt, ...);
int vformatear(char* buf, size_t max, const char* fmt, va_list ap);
uint32_t aleatorio(void);                   /* número pseudoaleatorio */

/* --- Archivos (rutas completas: "C:\\Mis documentos\\notas.txt") */
int archivo_tamano(const char* ruta);       /* -1 si no existe */
int archivo_leer(const char* ruta, void* buf, int max);
int archivo_escribir(const char* ruta, const void* datos, int bytes);

/* --- Ventanas y controles (coordenadas dentro de la ventana) */
int ventana_crear(const char* titulo, int ancho, int alto, int estilo);
void ventana_titulo(int ventana, const char* titulo);
void ventana_cerrar(int ventana);
int boton(int ventana, int x, int y, int ancho, int alto, const char* texto);
int etiqueta(int ventana, int x, int y, int ancho, int alto, const char* texto);
int campo_texto(int ventana, int x, int y, int ancho, int alto, const char* texto);
int casilla(int ventana, int x, int y, int ancho, int alto, const char* texto);
void control_texto(int control, const char* texto);
int control_leer(int control, char* buf, int max);
int casilla_marcada(int control);
void casilla_marcar(int control, int marcada);

/* --- Lienzo: una zona de la ventana para dibujar píxel a píxel (0xRRGGBB) */
typedef struct Lienzo {
    int ventana;
    uint32_t* px;
    int ancho, alto;
} Lienzo;
Lienzo* lienzo_crear(int ventana, int x, int y, int ancho, int alto);
void lienzo_mostrar(Lienzo* l);             /* hace visibles los cambios */
void lienzo_limpiar(Lienzo* l, uint32_t color);
void lienzo_pixel(Lienzo* l, int x, int y, uint32_t color);
void lienzo_rect(Lienzo* l, int x, int y, int ancho, int alto, uint32_t color);
void lienzo_marco(Lienzo* l, int x, int y, int ancho, int alto, uint32_t color);
void lienzo_linea(Lienzo* l, int x0, int y0, int x1, int y1, uint32_t color);
void lienzo_circulo(Lienzo* l, int cx, int cy, int r, uint32_t color, int relleno);
int lienzo_texto(Lienzo* l, int x, int y, const char* texto, uint32_t color);   /* devuelve el ancho */
void lienzo_imagen(Lienzo* l, int x, int y, const uint32_t* px, int ancho, int alto);
int texto_ancho(const char* texto);

/* --- Eventos, temporizadores y mensajes */
void temporizador(int ventana, int ms);     /* EV_TEMPORIZADOR cada 'ms'; 0 lo para */
int evento_esperar(Evento* e);              /* espera al siguiente evento */
int evento_consultar(Evento* e);            /* 1 si había un evento, 0 si no */
int mensaje(int ventana, const char* titulo, const char* texto, int botones);   /* RESPUESTA_* */

/* --- Bibliotecas del sistema (.bsl) */
const char* traducir(const char* texto);    /* lenguajes.bsl: texto en el idioma del sistema */
/* images.bsl: carga una imagen (BMP, PNG, JPEG, GIF). Devuelve los píxeles con malloc o 0. */
uint32_t* imagen_cargar(const char* ruta, int* ancho, int* alto);

int main(void);

#ifdef __cplusplus
}
#endif
#endif

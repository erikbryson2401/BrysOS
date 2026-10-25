/* Interfaz binaria entre BrysOS y los programas .run: números de las llamadas al sistema
 * (instrucción int 0x80: número en rax, argumentos en rdi, rsi, rdx, r10, r8, r9 y
 * resultado en rax), tipos de evento y estilos. La comparten el núcleo y el SDK. */
#ifndef BRYSOS_ABI_H
#define BRYSOS_ABI_H

#include <stdint.h>

#define BRYSOS_ABI_VERSION 1

enum {
    SYS_SALIR = 0,          /* (código) */
    SYS_DEPURAR,            /* (texto): escribe en el registro del sistema */
    SYS_MILISEGUNDOS,       /* () -> ms desde el arranque */
    SYS_DORMIR,             /* (ms) */
    SYS_MEMORIA,            /* (bytes) -> dirección de la memoria añadida al montón, o 0 */
    SYS_ARGUMENTOS,         /* (búfer, máx) -> longitud de la línea de órdenes */
    SYS_ARCHIVO_TAMANO,     /* (ruta) -> bytes o -1 */
    SYS_ARCHIVO_LEER,       /* (ruta, búfer, máx) -> bytes leídos o -1 */
    SYS_ARCHIVO_ESCRIBIR,   /* (ruta, datos, bytes) -> 0 o -1 */
    SYS_VENTANA_CREAR,      /* (título, ancho, alto, estilo) -> ventana o -1 */
    SYS_VENTANA_TITULO,     /* (ventana, título) */
    SYS_VENTANA_CERRAR,     /* (ventana) */
    SYS_CONTROL_CREAR,      /* (ventana, tipo, rect*, texto) -> control o -1 */
    SYS_CONTROL_TEXTO,      /* (control, texto) */
    SYS_CONTROL_LEER,       /* (control, búfer, máx) -> longitud */
    SYS_CONTROL_VALOR,      /* (control, valor o -1 para leer) -> valor (casilla marcada) */
    SYS_LIENZO_CREAR,       /* (ventana, rect*) -> dirección de los píxeles (0xRRGGBB) o 0 */
    SYS_LIENZO_MOSTRAR,     /* (ventana) */
    SYS_LIENZO_TEXTO,       /* (ventana, x, y, texto, color) -> ancho dibujado */
    SYS_TEMPORIZADOR,       /* (ventana, ms; 0 para parar) */
    SYS_EVENTO,             /* (evento*, esperar) -> 1 si hay evento, 0 si no */
    SYS_MENSAJE,            /* (ventana o 0, título, texto, botones) -> botón pulsado */
    SYS_TRADUCIR,           /* (texto, búfer, máx) -> longitud (lenguajes.bsl) */
    SYS_IMAGEN,             /* (ruta, píxeles, máx_píxeles, ancho*, alto*) -> 0 o -1 (images.bsl) */
    SYS_TEXTO_ANCHO,        /* (texto) -> ancho en píxeles con la fuente del sistema */
    SYS_NUM
};

/* Tipos de control */
enum { CONTROL_BOTON = 1, CONTROL_ETIQUETA, CONTROL_TEXTO, CONTROL_CASILLA };

/* Estilos de ventana */
enum { VENTANA_NORMAL = 0, VENTANA_FIJA = 1 };

/* Botones de los mensajes y resultado */
enum { MENSAJE_ACEPTAR = 0, MENSAJE_ACEPTAR_CANCELAR, MENSAJE_SI_NO, MENSAJE_SI_NO_CANCELAR };
enum { RESPUESTA_ACEPTAR = 1, RESPUESTA_CANCELAR, RESPUESTA_SI, RESPUESTA_NO };

/* Eventos */
enum {
    EV_NINGUNO = 0,
    EV_CERRAR,              /* el usuario quiere cerrar la ventana */
    EV_PULSADO,             /* botón pulsado o casilla cambiada (control) */
    EV_CAMBIO,              /* texto cambiado (control) */
    EV_INTRO,               /* Intro en un campo de texto (control) */
    EV_TECLA,               /* tecla pulsada (tecla, caracter) */
    EV_TECLA_SOLTADA,
    EV_RATON_ABAJO,         /* en el lienzo (x, y, botones) */
    EV_RATON_ARRIBA,
    EV_RATON_MOVER,
    EV_TEMPORIZADOR,
    EV_RUEDA                /* rueda del ratón en el lienzo (x, y; tecla = pasos, > 0 hacia abajo) */
};

typedef struct Evento {
    int32_t tipo;
    int32_t ventana;
    int32_t control;
    int32_t x, y;
    int32_t botones;
    int32_t tecla;          /* código de tecla (ver TECLA_*) */
    int32_t caracter;       /* carácter escrito (Latin-1) o 0 */
} Evento;

typedef struct Rect32 { int32_t x, y, ancho, alto; } Rect32;

/* Teclas especiales (coinciden con las del sistema) */
enum {
    TECLA_RETROCESO = 8, TECLA_TAB = 9, TECLA_INTRO = 13, TECLA_ESC = 27,
    TECLA_ARRIBA = 0x101, TECLA_ABAJO, TECLA_IZQUIERDA, TECLA_DERECHA,
    TECLA_INICIO, TECLA_FIN, TECLA_REPAG, TECLA_AVPAG, TECLA_INSERT, TECLA_SUPR,
    TECLA_F1,
    TECLA_MAYUS_IZQ = TECLA_F1 + 12, TECLA_MAYUS_DER, TECLA_CTRL_IZQ, TECLA_CTRL_DER
};

#define RUN_BASE      0x400000ull          /* dirección de carga de los programas */
#define RUN_PILA_TOPE 0x7FFFFFFFF000ull    /* cima de la pila */

#endif

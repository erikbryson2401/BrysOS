/*
 * Superficie "brysos" de libnsfb: NetSurf dibuja directamente en un lienzo de una ventana
 * de BrysOS (memoria compartida con el núcleo, 0x00RRGGBB = NSFB_FMT_XRGB8888) y recibe el
 * teclado y el ratón como eventos de BrysOS. Las llamadas son las de sdk/include/brysos_linux.h.
 *
 * Basada en src/surface/ram.c de libnsfb (Vincent Sanders, licencia MIT); esta parte
 * también se distribuye con licencia MIT.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "libnsfb.h"
#include "libnsfb_plot.h"
#include "libnsfb_event.h"

#include "nsfb.h"
#include "surface.h"
#include "plot.h"

#include "brysos_linux.h"

/* Códigos de tecla privados: un carácter ya traducido por BrysOS (con la distribución de
 * teclado del usuario) viaja como NSFB_KEY_BRYS_CHAR + carácter; ver fbtk/event.c. */
#define NSFB_KEY_BRYS_CHAR 0x10000

static int ventana = -1;
static int botones;                 /* botones del ratón pulsados (MB_*) */
static bool ctrl_abajo;

/* un evento de BrysOS puede dar varios de libnsfb (mover + pulsar) */
static nsfb_event_t cola[8];
static int cola_n;

static void encolar(enum nsfb_event_type_e tipo, int valor, int x, int y)
{
    if (cola_n >= (int)(sizeof cola / sizeof cola[0]))
        return;
    nsfb_event_t *e = &cola[cola_n++];
    memset(e, 0, sizeof *e);
    e->type = tipo;
    if (tipo == NSFB_EVENT_MOVE_ABSOLUTE) {
        e->value.vector.x = x;
        e->value.vector.y = y;
    } else {
        e->value.keycode = valor;
    }
}

static int brysos_defaults(nsfb_t *nsfb)
{
    nsfb->width = 800;
    nsfb->height = 600;
    nsfb->format = NSFB_FMT_XRGB8888;
    select_plotters(nsfb);
    return 0;
}

static int brysos_initialise(nsfb_t *nsfb)
{
    if (ventana >= 0)
        return -1;
    nsfb->format = NSFB_FMT_XRGB8888;
    nsfb->bpp = 32;
    select_plotters(nsfb);

    ventana = brys_ventana_crear("NetSurf", nsfb->width, nsfb->height, VENTANA_FIJA);
    if (ventana < 0)
        return -1;
    Rect32 r = { 0, 0, nsfb->width, nsfb->height };
    uint32_t *px = brys_lienzo_crear(ventana, &r);
    if (px == NULL) {
        brys_ventana_cerrar(ventana);
        ventana = -1;
        return -1;
    }
    nsfb->ptr = (uint8_t *)px;
    nsfb->linelen = nsfb->width * 4;
    return 0;
}

static int brysos_set_geometry(nsfb_t *nsfb, int width, int height, enum nsfb_format_e format)
{
    /* el lienzo es fijo una vez creado */
    if (nsfb->ptr != NULL)
        return (width == nsfb->width && height == nsfb->height) ? 0 : -1;
    if (width > 0)
        nsfb->width = width;
    if (height > 0)
        nsfb->height = height;
    (void)format;
    nsfb->format = NSFB_FMT_XRGB8888;
    select_plotters(nsfb);
    nsfb->linelen = nsfb->width * 4;
    return 0;
}

static int brysos_finalise(nsfb_t *nsfb)
{
    if (ventana >= 0)
        brys_ventana_cerrar(ventana);
    ventana = -1;
    nsfb->ptr = NULL;           /* la memoria del lienzo es del núcleo */
    return 0;
}

static int tecla_nsfb(int tecla)
{
    switch (tecla) {
    case TECLA_RETROCESO: return NSFB_KEY_BACKSPACE;
    case TECLA_TAB: return NSFB_KEY_TAB;
    case TECLA_INTRO: return NSFB_KEY_RETURN;
    case TECLA_ESC: return NSFB_KEY_ESCAPE;
    case TECLA_ARRIBA: return NSFB_KEY_UP;
    case TECLA_ABAJO: return NSFB_KEY_DOWN;
    case TECLA_IZQUIERDA: return NSFB_KEY_LEFT;
    case TECLA_DERECHA: return NSFB_KEY_RIGHT;
    case TECLA_INICIO: return NSFB_KEY_HOME;
    case TECLA_FIN: return NSFB_KEY_END;
    case TECLA_REPAG: return NSFB_KEY_PAGEUP;
    case TECLA_AVPAG: return NSFB_KEY_PAGEDOWN;
    case TECLA_INSERT: return NSFB_KEY_INSERT;
    case TECLA_SUPR: return NSFB_KEY_DELETE;
    case TECLA_MAYUS_IZQ: return NSFB_KEY_LSHIFT;
    case TECLA_MAYUS_DER: return NSFB_KEY_RSHIFT;
    case TECLA_CTRL_IZQ: return NSFB_KEY_LCTRL;
    case TECLA_CTRL_DER: return NSFB_KEY_RCTRL;
    default:
        if (tecla >= TECLA_F1 && tecla < TECLA_F1 + 12)
            return NSFB_KEY_F1 + (tecla - TECLA_F1);
        if (tecla >= 'a' && tecla <= 'z')
            return tecla;
        return 0;
    }
}

static void traducir(const Evento *ev)
{
    static const int boton_nsfb[3] = { NSFB_KEY_MOUSE_1, NSFB_KEY_MOUSE_3, NSFB_KEY_MOUSE_2 };
    int k, cambio;

    switch (ev->tipo) {
    case EV_CERRAR:
        cola_n = 0;
        encolar(NSFB_EVENT_CONTROL, 0, 0, 0);
        cola[0].value.controlcode = NSFB_CONTROL_QUIT;
        break;

    case EV_TECLA:
    case EV_TECLA_SOLTADA: {
        bool abajo = ev->tipo == EV_TECLA;
        if (ev->tecla == TECLA_CTRL_IZQ || ev->tecla == TECLA_CTRL_DER)
            ctrl_abajo = abajo;
        k = tecla_nsfb(ev->tecla);
        /* los caracteres imprimibles llegan ya traducidos (salvo con Ctrl: atajos) */
        if (ev->caracter >= 32 && ev->caracter != 127 && !ctrl_abajo)
            k = NSFB_KEY_BRYS_CHAR + (ev->caracter & 0xFF);
        if (k)
            encolar(abajo ? NSFB_EVENT_KEY_DOWN : NSFB_EVENT_KEY_UP, k, 0, 0);
        break;
    }

    case EV_RATON_MOVER:
        encolar(NSFB_EVENT_MOVE_ABSOLUTE, 0, ev->x, ev->y);
        break;

    case EV_RATON_ABAJO:
    case EV_RATON_ARRIBA:
        encolar(NSFB_EVENT_MOVE_ABSOLUTE, 0, ev->x, ev->y);
        cambio = (ev->botones ^ botones) & 7;
        for (int i = 0; i < 3; i++)
            if (cambio & (1 << i))
                encolar((ev->botones & (1 << i)) ? NSFB_EVENT_KEY_DOWN : NSFB_EVENT_KEY_UP,
                        boton_nsfb[i], 0, 0);
        botones = ev->botones & 7;
        break;

    case EV_RUEDA: {
        /* la rueda es como los botones 4 y 5 de X11/SDL: un clic por paso */
        int pasos = ev->tecla;
        int b = pasos > 0 ? NSFB_KEY_MOUSE_5 : NSFB_KEY_MOUSE_4;
        if (pasos < 0)
            pasos = -pasos;
        if (pasos > 3)
            pasos = 3;
        encolar(NSFB_EVENT_MOVE_ABSOLUTE, 0, ev->x, ev->y);
        for (int i = 0; i < pasos; i++) {
            encolar(NSFB_EVENT_KEY_DOWN, b, 0, 0);
            encolar(NSFB_EVENT_KEY_UP, b, 0, 0);
        }
        break;
    }

    default:
        break;
    }
}

static long ahora_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000L + t.tv_nsec / 1000000L;
}

static bool brysos_input(nsfb_t *nsfb, nsfb_event_t *event, int timeout)
{
    (void)nsfb;
    long fin = timeout > 0 ? ahora_ms() + timeout : 0;
    for (;;) {
        if (cola_n > 0) {
            *event = cola[0];
            memmove(cola, cola + 1, (size_t)(--cola_n) * sizeof cola[0]);
            return true;
        }
        Evento ev;
        if (brys_evento(&ev, timeout < 0)) {
            traducir(&ev);
            continue;
        }
        if (timeout == 0 || (timeout > 0 && ahora_ms() >= fin)) {
            event->type = NSFB_EVENT_CONTROL;
            event->value.controlcode = NSFB_CONTROL_TIMEOUT;
            return true;
        }
        struct timespec espera = { 0, 5 * 1000000L };
        nanosleep(&espera, NULL);
    }
}

static int brysos_update(nsfb_t *nsfb, nsfb_bbox_t *box)
{
    (void)nsfb;
    (void)box;
    if (ventana >= 0)
        brys_lienzo_mostrar(ventana);
    return 0;
}

const nsfb_surface_rtns_t brysos_rtns = {
    .defaults = brysos_defaults,
    .initialise = brysos_initialise,
    .finalise = brysos_finalise,
    .input = brysos_input,
    .geometry = brysos_set_geometry,
    .update = brysos_update,
};

NSFB_SURFACE_DEF(brysos, NSFB_SURFACE_LINUX, &brysos_rtns)

/*
 * Al terminar: el navegador más pequeño del mundo cabe en una ventana de 800x600 y en
 * cuarenta líneas de traducción de eventos. Lo difícil fue todo lo demás.
 */

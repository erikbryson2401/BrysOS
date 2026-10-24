/* Serpiente.run: el juego de la serpiente, dibujado en un lienzo */
#include <brysos.h>

#define COLS 24
#define FILAS 20
#define CELDA 16
#define MARCADOR 24
#define MAX (COLS * FILAS)

static int sx[MAX], sy[MAX];
static int largo, dir, dir_nueva, fx, fy, puntos, record, vivo, pausa, esperando;
static Lienzo* l;

enum { DER, IZQ, ARR, ABA };

static int ocupada(int x, int y) {
    for (int i = 0; i < largo; i++)
        if (sx[i] == x && sy[i] == y) return 1;
    return 0;
}

static void poner_fruta(void) {
    do {
        fx = (int)(aleatorio() % COLS);
        fy = (int)(aleatorio() % FILAS);
    } while (ocupada(fx, fy));
}

static void empezar(void) {
    largo = 4;
    for (int i = 0; i < largo; i++) {
        sx[i] = COLS / 2 - i;
        sy[i] = FILAS / 2;
    }
    dir = dir_nueva = DER;
    puntos = 0;
    vivo = 1;
    pausa = 0;
    esperando = 1;   /* no se mueve hasta que se pulsa una flecha */
    poner_fruta();
}

static void dibujar(void) {
    char txt[80];
    lienzo_rect(l, 0, 0, COLS * CELDA, MARCADOR, 0x000080);
    formatear(txt, sizeof txt, "%s: %d    %s: %d", traducir("Puntos"), puntos, traducir("Récord"), record);
    lienzo_texto(l, 8, 5, txt, 0xFFFFFF);
    if (pausa) lienzo_texto(l, COLS * CELDA - 70, 5, traducir("Pausa"), 0xFFFF00);
    lienzo_rect(l, 0, MARCADOR, COLS * CELDA, FILAS * CELDA, 0x004000);
    for (int y = 0; y < FILAS; y++)
        for (int x = (y & 1); x < COLS; x += 2)
            lienzo_rect(l, x * CELDA, MARCADOR + y * CELDA, CELDA, CELDA, 0x004800);
    lienzo_circulo(l, fx * CELDA + CELDA / 2, MARCADOR + fy * CELDA + CELDA / 2, CELDA / 2 - 2, 0xE02020, 1);
    for (int i = largo - 1; i >= 0; i--) {
        uint32_t c = i == 0 ? 0xFFFF40 : (i & 1 ? 0x40C040 : 0x30A030);
        lienzo_rect(l, sx[i] * CELDA + 1, MARCADOR + sy[i] * CELDA + 1, CELDA - 2, CELDA - 2, c);
    }
    if (!vivo || esperando) {
        const char* t = vivo ? traducir("Pulse una flecha para empezar") : traducir("Fin del juego - pulse Intro");
        int w = texto_ancho(t);
        lienzo_rect(l, (COLS * CELDA - w) / 2 - 8, MARCADOR + FILAS * CELDA / 2 - 14, w + 16, 26, 0xC0C0C0);
        lienzo_marco(l, (COLS * CELDA - w) / 2 - 8, MARCADOR + FILAS * CELDA / 2 - 14, w + 16, 26, 0x000000);
        lienzo_texto(l, (COLS * CELDA - w) / 2, MARCADOR + FILAS * CELDA / 2 - 7, t, 0x000000);
    }
    lienzo_mostrar(l);
}

static void avanzar(void) {
    if (!vivo || pausa || esperando) return;
    dir = dir_nueva;
    int nx = sx[0] + (dir == DER) - (dir == IZQ);
    int ny = sy[0] + (dir == ABA) - (dir == ARR);
    int come = nx == fx && ny == fy;
    /* la cola se mueve en este paso, así que no cuenta como choque (salvo si crece) */
    int cola = largo - (come ? 0 : 1);
    int choque = nx < 0 || ny < 0 || nx >= COLS || ny >= FILAS;
    for (int i = 0; i < cola && !choque; i++)
        if (sx[i] == nx && sy[i] == ny) choque = 1;
    if (choque) {
        vivo = 0;
        if (puntos > record) record = puntos;
        return;
    }
    if (come && largo < MAX) largo++;
    for (int i = largo - 1; i > 0; i--) {
        sx[i] = sx[i - 1];
        sy[i] = sy[i - 1];
    }
    sx[0] = nx;
    sy[0] = ny;
    if (come) {
        puntos += 10;
        if (largo < MAX) poner_fruta();
    }
}

int main(void) {
    int v = ventana_crear(traducir("Serpiente"), COLS * CELDA + 8, MARCADOR + FILAS * CELDA + 8, VENTANA_FIJA);
    if (v < 0) return 1;
    l = lienzo_crear(v, 4, 4, COLS * CELDA, MARCADOR + FILAS * CELDA);
    if (!l) return 1;
    empezar();
    dibujar();
    temporizador(v, 120);
    Evento e;
    while (evento_esperar(&e)) {
        if (e.tipo == EV_CERRAR) break;
        if (e.tipo == EV_TEMPORIZADOR) {
            avanzar();
            dibujar();
        } else if (e.tipo == EV_TECLA) {
            /* la primera flecha pone en marcha la serpiente (hacia la izquierda no: no puede darse la vuelta) */
            if (vivo && esperando && e.tecla >= TECLA_ARRIBA && e.tecla <= TECLA_DERECHA) esperando = 0;
            switch (e.tecla) {
            case TECLA_ARRIBA: if (dir != ABA) dir_nueva = ARR; break;
            case TECLA_ABAJO: if (dir != ARR) dir_nueva = ABA; break;
            case TECLA_IZQUIERDA: if (dir != DER) dir_nueva = IZQ; break;
            case TECLA_DERECHA: if (dir != IZQ) dir_nueva = DER; break;
            case TECLA_INTRO: if (!vivo) { empezar(); dibujar(); } break;
            case TECLA_ESC: goto fin;
            default:
                if (e.caracter == 'p' || e.caracter == 'P' || e.caracter == ' ') {
                    if (vivo) pausa = !pausa;
                    dibujar();
                }
                break;
            }
        }
    }
fin:
    return 0;
}

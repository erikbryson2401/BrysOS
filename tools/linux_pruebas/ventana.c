/* Un programa de Linux con ventana de BrysOS: crea un lienzo, pinta un degradado con la
 * hora de Linux (time/localtime) y espera a que se cierre. */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "../../sdk/include/brysos_linux.h"

int main(void) {
    int v = brys_ventana_crear("Linux en BrysOS", 360, 200, VENTANA_FIJA);
    if (v < 0) { puts("no se ha podido crear la ventana"); return 1; }
    Rect32 r = {0, 0, 360, 200};
    uint32_t* px = brys_lienzo_crear(v, &r);
    if (!px) { puts("no se ha podido crear el lienzo"); return 1; }
    for (int y = 0; y < 200; y++)
        for (int x = 0; x < 360; x++) px[y * 360 + x] = ((uint32_t)(x * 255 / 359) << 16) | ((uint32_t)(y * 255 / 199) << 8) | 0x60;
    time_t t = time(NULL);
    char buf[64];
    strftime(buf, sizeof buf, "%H:%M:%S (UTC)", gmtime(&t));
    brys_lienzo_texto(v, 20, 80, "Hola: soy un programa de Linux", 0xFFFFFF);
    brys_lienzo_texto(v, 20, 100, buf, 0xFFFFFF);
    brys_lienzo_mostrar(v);
    puts("ventana dibujada; PRUEBA VENTANA: TODO BIEN");
    fflush(stdout);
    Evento e;
    while (brys_evento(&e, 1) && e.tipo != EV_CERRAR) {}
    return 0;
}

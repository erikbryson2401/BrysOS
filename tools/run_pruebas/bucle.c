/* Prueba: un programa que se queda colgado (bucle infinito sin atender eventos) */
#include <brysos.h>

int main(void) {
    ventana_crear("Bucle", 260, 100, VENTANA_FIJA);
    depurar("entrando en el bucle infinito");
    volatile uint64_t n = 0;
    for (;;) n++;
}

/* Prueba: un programa que hace una operación no válida (lee la memoria del núcleo, escribe en
 * una dirección sin asignar y, si sigue vivo, divide por cero) */
#include <brysos.h>

int main(void) {
    const char* a = argumentos();
    ventana_crear("Fallo", 260, 100, VENTANA_FIJA);
    dormir(500);
    if (a[0] == 'n') {
        volatile uint64_t* k = (volatile uint64_t*)0xFFFFFFFF80000000ull;
        depurar("leyendo la memoria del núcleo");
        return (int)*k;
    }
    if (a[0] == 'd') {
        volatile int cero = 0;
        depurar("dividiendo por cero");
        return 10 / cero;
    }
    depurar("escribiendo en una dirección sin asignar");
    *(volatile int*)0x1234 = 1;
    return 0;
}

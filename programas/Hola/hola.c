/* Hola.run: ejemplo de programa con controles, mensajes y traducciones */
#include <brysos.h>

int main(void) {
    int v = ventana_crear(traducir("Hola"), 330, 178, VENTANA_FIJA);
    if (v < 0) return 1;
    etiqueta(v, 12, 14, 300, 16, traducir("¿Cómo te llamas?"));
    int nombre = campo_texto(v, 12, 34, 300, 22, "");
    int mayus = casilla(v, 12, 64, 300, 18, traducir("Saludar en mayúsculas"));
    int saludo = etiqueta(v, 12, 92, 300, 32, traducir("Escribe tu nombre y pulsa Saludar."));
    int b_saludar = boton(v, 12, 134, 96, 24, traducir("&Saludar"));
    int b_acerca = boton(v, 116, 134, 96, 24, traducir("&Acerca de"));
    int b_salir = boton(v, 220, 134, 96, 24, traducir("Salir"));

    int veces = 0;
    Evento e;
    while (evento_esperar(&e)) {
        if (e.tipo == EV_CERRAR) break;
        if (e.tipo == EV_PULSADO && e.control == b_salir) break;
        if ((e.tipo == EV_PULSADO && e.control == b_saludar) || (e.tipo == EV_INTRO && e.control == nombre)) {
            char n[64], txt[160];
            control_leer(nombre, n, sizeof n);
            if (!n[0]) {
                mensaje(v, traducir("Hola"), traducir("Primero escribe tu nombre."), MENSAJE_ACEPTAR);
                continue;
            }
            if (casilla_marcada(mayus))
                for (char* c = n; *c; c++)
                    if (*c >= 'a' && *c <= 'z') *c -= 32;
            veces++;
            formatear(txt, sizeof txt, traducir("¡Hola, %s! (saludo número %d)"), n, veces);
            control_texto(saludo, txt);
            formatear(txt, sizeof txt, "%s - %s", traducir("Hola"), n);
            ventana_titulo(v, txt);
        } else if (e.tipo == EV_PULSADO && e.control == b_acerca) {
            char txt[200];
            formatear(txt, sizeof txt, traducir("Programa de ejemplo de BrysOS.\n\nLleva %d segundos en marcha."),
                      (int)(milisegundos() / 1000));
            mensaje(v, traducir("Acerca de Hola"), txt, MENSAJE_ACEPTAR);
        }
    }
    return 0;
}

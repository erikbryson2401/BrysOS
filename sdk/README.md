# SDK de BrysOS: programas `.run`

Un `.run` es el equivalente de un `.exe`: un programa de 64 bits que BrysOS ejecuta en
modo usuario, separado del núcleo. Si falla (por ejemplo, accede a memoria que no es suya o
divide por cero), BrysOS lo cierra y muestra "Este programa ha realizado una operación no
válida y se cerrará"; el resto del sistema sigue funcionando. Si se queda colgado, se puede
finalizar con **Ctrl+Alt+Supr** ("Cerrar programa").

## Crear un programa

1. Cree una carpeta en `programas/` con el nombre del programa, por ejemplo
   `programas/Cronometro/`, y ponga dentro uno o más archivos `.c`.
2. Incluya `<brysos.h>` y escriba `int main(void)`.
3. Ejecute `make`: se genera `build/run/Cronometro.run`, va dentro del sistema y se instala en
   `C:\Archivos de programa` (aparece en Inicio > Programas). También puede copiar el
   `.run` a un pendrive y abrirlo con doble clic.

```c
#include <brysos.h>

int main(void) {
    int v = ventana_crear("Tiempo encendido", 200, 80, VENTANA_FIJA);
    int e = etiqueta(v, 12, 20, 170, 16, "");
    temporizador(v, 1000);
    Evento ev;
    while (evento_esperar(&ev) && ev.tipo != EV_CERRAR) {
        char t[32];
        uint64_t s = milisegundos() / 1000;
        formatear(t, sizeof t, "%02d:%02d:%02d", (int)(s / 3600), (int)(s / 60 % 60), (int)(s % 60));
        control_texto(e, t);
    }
    return 0;
}
```

## Qué ofrece `brysos.h`

| Grupo | Funciones |
|---|---|
| Sistema | `salir`, `depurar` (al registro), `milisegundos`, `dormir`, `argumentos` |
| Memoria y cadenas | `malloc`, `calloc`, `realloc`, `free`, `memcpy`, `memset`, `strlen`, `strcmp`, `formatear` (como `snprintf`), `numero`, `aleatorio` |
| Archivos | `archivo_tamano`, `archivo_leer`, `archivo_escribir` (rutas como `C:\\Mis documentos\\a.txt`) |
| Ventanas | `ventana_crear`, `ventana_titulo`, `ventana_cerrar`, `boton`, `etiqueta`, `campo_texto`, `casilla`, `control_texto`, `control_leer`, `casilla_marcada` |
| Lienzo | `lienzo_crear`, `lienzo_pixel`, `lienzo_rect`, `lienzo_linea`, `lienzo_circulo`, `lienzo_texto`, `lienzo_imagen`, `lienzo_mostrar` |
| Eventos | `evento_esperar`, `evento_consultar`, `temporizador`, `mensaje` |
| Bibliotecas `.bsl` | `traducir` (lenguajes.bsl), `imagen_cargar` (images.bsl: BMP, PNG, JPEG, GIF) |

Los eventos (`EV_CERRAR`, `EV_PULSADO`, `EV_TECLA`, `EV_RATON_ABAJO`, `EV_TEMPORIZADOR`...)
llegan con `evento_esperar`. Un programa que tarda más de 5 segundos en atender sus eventos
aparece como "no responde".

Ejemplos: `programas/Hola` (controles y mensajes) y `programas/Serpiente` (juego en un
lienzo con temporizador y teclado).

## Detalles técnicos

- Formato: ELF64 estático cargado en `0x400000` (`sdk/run.ld`), entrada en `sdk/lib/crt0.S`.
- Llamadas al sistema: `int 0x80`, número en `rax`, argumentos en `rdi, rsi, rdx, r10, r8`
  (`sdk/include/brysos_abi.h`).
- Cada programa tiene su propio espacio de direcciones, una pila de 1 MB y hasta 1 GB de
  montón; el sistema reparte el procesador entre los programas cada 10 ms.
- Los textos se compilan en Latin-1 (`-fexec-charset=ISO-8859-1`), como el resto del sistema.

/* Prueba: memoria (malloc/free/realloc), cadenas, formatear, archivos y SSE */
#include <brysos.h>

static int fallos;
static void comprobar(int ok, const char* que) {
    char t[160];
    formatear(t, sizeof t, "%s: %s", que, ok ? "bien" : "MAL");
    depurar(t);
    if (!ok) fallos++;
}

int main(void) {
    char t[128];
    formatear(t, sizeof t, "[%5d|%-3d|%05d|%x|%X|%lu|%s|%c|%%]", 42, 7, -42, 255, 0xBEEF, 1234567890123ull, "ok", 'z');
    comprobar(!strcmp(t, "[   42|7  |-0042|ff|BEEF|1234567890123|ok|z|%]"), t);
    /* muchos bloques de tamaños variados, liberados en desorden */
    enum { N = 2000 };
    static unsigned char* p[N];
    static size_t tam[N];
    for (int i = 0; i < N; i++) {
        tam[i] = 1 + aleatorio() % 3000;
        p[i] = malloc(tam[i]);
        if (!p[i]) { comprobar(0, "malloc"); break; }
        memset(p[i], (int)(i & 255), tam[i]);
    }
    for (int i = 0; i < N; i += 2) { free(p[i]); p[i] = 0; }
    for (int i = 1; i < N; i += 2) {
        int ok = 1;
        for (size_t j = 0; j < tam[i]; j++) if (p[i][j] != (unsigned char)(i & 255)) ok = 0;
        if (!ok) { comprobar(0, "contenido de los bloques"); break; }
        p[i] = realloc(p[i], tam[i] * 3);
        for (size_t j = 0; j < tam[i]; j++) if (p[i][j] != (unsigned char)(i & 255)) ok = 0;
        if (!ok) { comprobar(0, "realloc"); break; }
    }
    for (int i = 0; i < N; i++) free(p[i]);
    void* grande = malloc(64u << 20);
    comprobar(grande != 0, "64 MB de memoria");
    if (grande) { memset(grande, 0x5A, 64u << 20); free(grande); }
    /* coma flotante (SSE): el sistema guarda los registros en cada cambio de programa */
    volatile double x = 0;
    for (int i = 1; i <= 30000000; i++) x += 1.0 / ((double)i * i);
    comprobar(x > 1.6449 && x < 1.64494, "coma flotante");
    /* archivos */
    const char* ruta = "C:\\Mis documentos\\prueba_run.txt";
    const char* texto = "Escrito por un programa .run\r\n";
    comprobar(archivo_escribir(ruta, texto, (int)strlen(texto)) == 0, "escribir archivo");
    char buf[100] = {0};
    comprobar(archivo_tamano(ruta) == (int)strlen(texto), "tamaño del archivo");
    comprobar(archivo_leer(ruta, buf, sizeof buf - 1) == (int)strlen(texto) && !strcmp(buf, texto), "leer archivo");
    comprobar(archivo_tamano("C:\\no existe.txt") == -1, "archivo que no existe");
    /* punteros no válidos: el sistema los rechaza sin cerrar el programa */
    comprobar(archivo_leer(ruta, (void*)0xFFFF800000000000ull, 10) == -1, "búfer del núcleo rechazado");
    comprobar(archivo_tamano((const char*)0x10) == -1, "ruta no válida rechazada");
    formatear(t, sizeof t, "PRUEBA MEMORIA: %s", fallos ? "HAY FALLOS" : "TODO BIEN");
    depurar(t);
    return fallos;
}

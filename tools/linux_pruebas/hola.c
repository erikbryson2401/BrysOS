/* Primer programa de Linux en BrysOS: compilado con musl, sin nada de BrysOS. Prueba la
 * salida estándar, la memoria (malloc grande = mmap), los archivos, el tiempo y getenv. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

int main(int argc, char** argv) {
    printf("Hola desde Linux (musl) en BrysOS! argc=%d argv[0]=%s\n", argc, argv[0]);
    char* big = malloc(4 << 20);
    if (!big) { puts("FALLO malloc"); return 1; }
    memset(big, 'x', 4 << 20);
    printf("malloc de 4 MiB: %s\n", big[(4 << 20) - 1] == 'x' ? "bien" : "MAL");
    free(big);
    FILE* f = fopen("/c/prueba-linux.txt", "w");
    if (!f) { perror("fopen"); return 1; }
    fprintf(f, "escrito con fprintf: %d %.3f\n", 42, 3.14159);
    fclose(f);
    f = fopen("/c/prueba-linux.txt", "r");
    char line[128] = "";
    if (f) { fgets(line, sizeof line, f); fclose(f); }
    printf("leido: %s", line);
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    printf("tiempo monotonico: %ld s, HOME=%s, pid=%d\n", (long)ts.tv_sec, getenv("HOME") ? getenv("HOME") : "(nada)", (int)getpid());
    puts("PRUEBA LINUX: TODO BIEN");
    return 0;
}

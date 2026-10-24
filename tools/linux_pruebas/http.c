/* Red desde un programa de Linux: getaddrinfo (DNS de musl por UDP), socket TCP, connect,
 * send y recv. Descarga la página principal de un servidor por HTTP y cuenta los bytes. */
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc, char** argv) {
    const char* host = argc > 1 ? argv[1] : "google.com";
    struct addrinfo hints = {0}, *res;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    int e = getaddrinfo(host, "80", &hints, &res);
    if (e) { printf("getaddrinfo(%s): %s\n", host, gai_strerror(e)); return 1; }
    char ip[64];
    getnameinfo(res->ai_addr, res->ai_addrlen, ip, sizeof ip, NULL, 0, NI_NUMERICHOST);
    printf("%s es %s\n", host, ip);
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) { perror("socket"); return 1; }
    if (connect(s, res->ai_addr, res->ai_addrlen) < 0) { perror("connect"); return 1; }
    char req[256];
    int n = snprintf(req, sizeof req, "GET / HTTP/1.0\r\nHost: %s\r\nUser-Agent: BrysOS/0.7\r\n\r\n", host);
    if (send(s, req, n, 0) != n) { perror("send"); return 1; }
    char buf[4096];
    long total = 0;
    char status[128] = "";
    for (;;) {
        ssize_t r = recv(s, buf, sizeof buf, 0);
        if (r < 0) { perror("recv"); return 1; }
        if (r == 0) break;
        if (!total) {
            char* nl = memchr(buf, '\r', r);
            size_t k = nl ? (size_t)(nl - buf) : (size_t)r;
            if (k >= sizeof status) k = sizeof status - 1;
            memcpy(status, buf, k);
            status[k] = 0;
        }
        total += r;
    }
    close(s);
    freeaddrinfo(res);
    printf("respuesta: \"%s\", %ld bytes\n", status, total);
    if (!strncmp(status, "HTTP/1.", 7) && total > 100) puts("PRUEBA HTTP: TODO BIEN");
    return 0;
}

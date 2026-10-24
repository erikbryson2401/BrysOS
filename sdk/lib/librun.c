/* Biblioteca de los programas .run: llamadas al sistema, memoria, cadenas y dibujo */
#include "brysos.h"

static inline int64_t llamar(uint64_t n, uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e) {
    int64_t r;
    register uint64_t r10 __asm__("r10") = d;
    register uint64_t r8 __asm__("r8") = e;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8) : "memory");
    return r;
}
#define L0(n) llamar((n), 0, 0, 0, 0, 0)
#define L1(n, a) llamar((n), (uint64_t)(a), 0, 0, 0, 0)
#define L2(n, a, b) llamar((n), (uint64_t)(a), (uint64_t)(b), 0, 0, 0)
#define L3(n, a, b, c) llamar((n), (uint64_t)(a), (uint64_t)(b), (uint64_t)(c), 0, 0)
#define L4(n, a, b, c, d) llamar((n), (uint64_t)(a), (uint64_t)(b), (uint64_t)(c), (uint64_t)(d), 0)
#define L5(n, a, b, c, d, e) llamar((n), (uint64_t)(a), (uint64_t)(b), (uint64_t)(c), (uint64_t)(d), (uint64_t)(e))

static const char* linea_ordenes = "";
static uint32_t semilla;

void __run_iniciar(const char* args) {
    if (args) linea_ordenes = args;
    semilla = (uint32_t)milisegundos() * 2654435761u + 1;
}

/* ---------------------------------------------------------------- sistema */
void salir(int codigo) {
    L1(SYS_SALIR, codigo);
    for (;;) {}
}
void depurar(const char* texto) { L1(SYS_DEPURAR, texto); }
uint64_t milisegundos(void) { return (uint64_t)L0(SYS_MILISEGUNDOS); }
void dormir(uint32_t ms) { L1(SYS_DORMIR, ms); }
const char* argumentos(void) { return linea_ordenes; }

uint32_t aleatorio(void) {
    /* xorshift32 */
    uint32_t x = semilla;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return semilla = x;
}

/* ---------------------------------------------------------------- memoria
 * Montón sencillo: bloques con cabecera en una lista libre ordenada por dirección;
 * al liberar se unen los bloques vecinos. Crece con SYS_MEMORIA. */
typedef struct Bloque {
    size_t tam;             /* bytes útiles tras la cabecera */
    struct Bloque* sig;     /* siguiente libre (sólo en la lista libre) */
} Bloque;
#define CABECERA 16
static Bloque* libres;

static void insertar_libre(Bloque* b) {
    Bloque** pp = &libres;
    while (*pp && *pp < b) pp = &(*pp)->sig;
    b->sig = *pp;
    *pp = b;
    /* unir con el siguiente */
    if (b->sig && (char*)b + CABECERA + b->tam == (char*)b->sig) {
        b->tam += CABECERA + b->sig->tam;
        b->sig = b->sig->sig;
    }
    /* unir con el anterior */
    if (pp != &libres) {
        Bloque* ant = (Bloque*)((char*)pp - __builtin_offsetof(Bloque, sig));
        if ((char*)ant + CABECERA + ant->tam == (char*)b) {
            ant->tam += CABECERA + b->tam;
            ant->sig = b->sig;
        }
    }
}

void* malloc(size_t n) {
    if (!n) n = 1;
    n = (n + 15) & ~(size_t)15;
    for (int intento = 0; intento < 2; intento++) {
        for (Bloque** pp = &libres; *pp; pp = &(*pp)->sig) {
            Bloque* b = *pp;
            if (b->tam < n) continue;
            if (b->tam >= n + CABECERA + 32) {
                Bloque* resto = (Bloque*)((char*)b + CABECERA + n);
                resto->tam = b->tam - n - CABECERA;
                resto->sig = b->sig;
                b->tam = n;
                *pp = resto;
            } else {
                *pp = b->sig;
            }
            return (char*)b + CABECERA;
        }
        /* pide más memoria al sistema (al menos 256 KB) */
        size_t pedir = n + CABECERA;
        if (pedir < (256u << 10)) pedir = 256u << 10;
        pedir = (pedir + 4095) & ~(size_t)4095;
        Bloque* nuevo = (Bloque*)L1(SYS_MEMORIA, pedir);
        if (!nuevo) return 0;
        nuevo->tam = pedir - CABECERA;
        insertar_libre(nuevo);
    }
    return 0;
}

void free(void* p) {
    if (!p) return;
    insertar_libre((Bloque*)((char*)p - CABECERA));
}

void* calloc(size_t n, size_t tam) {
    if (tam && n > (size_t)-1 / tam) return 0;
    void* p = malloc(n * tam);
    if (p) memset(p, 0, n * tam);
    return p;
}

void* realloc(void* p, size_t n) {
    if (!p) return malloc(n);
    if (!n) { free(p); return 0; }
    Bloque* b = (Bloque*)((char*)p - CABECERA);
    if (b->tam >= n) return p;
    void* q = malloc(n);
    if (!q) return 0;
    memcpy(q, p, b->tam);
    free(p);
    return q;
}

/* ---------------------------------------------------------------- cadenas */
void* memcpy(void* d, const void* s, size_t n) {
    void* r = d;
    __asm__ volatile("rep movsb" : "+D"(d), "+S"(s), "+c"(n) : : "memory");
    return r;
}
void* memmove(void* d, const void* s, size_t n) {
    unsigned char* dd = d;
    const unsigned char* ss = s;
    if (dd <= ss || dd >= ss + n) return memcpy(d, s, n);
    while (n--) dd[n] = ss[n];
    return d;
}
void* memset(void* d, int c, size_t n) {
    void* r = d;
    __asm__ volatile("rep stosb" : "+D"(d), "+c"(n) : "a"(c) : "memory");
    return r;
}
int memcmp(const void* a, const void* b, size_t n) {
    const unsigned char *x = a, *y = b;
    for (size_t i = 0; i < n; i++)
        if (x[i] != y[i]) return x[i] - y[i];
    return 0;
}
size_t strlen(const char* s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}
char* strcpy(char* d, const char* s) {
    char* r = d;
    while ((*d++ = *s++)) {}
    return r;
}
char* strncpy(char* d, const char* s, size_t n) {
    size_t i = 0;
    for (; i < n && s[i]; i++) d[i] = s[i];
    for (; i < n; i++) d[i] = 0;
    return d;
}
char* strcat(char* d, const char* s) {
    strcpy(d + strlen(d), s);
    return d;
}
int strcmp(const char* a, const char* b) {
    while (*a && *a == *b) a++, b++;
    return (unsigned char)*a - (unsigned char)*b;
}
int strncmp(const char* a, const char* b, size_t n) {
    for (; n; n--, a++, b++) {
        if (*a != *b) return (unsigned char)*a - (unsigned char)*b;
        if (!*a) return 0;
    }
    return 0;
}
int numero(const char* s) {
    while (*s == ' ' || *s == '\t') s++;
    int neg = 0, v = 0;
    if (*s == '-' || *s == '+') neg = *s++ == '-';
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    return neg ? -v : v;
}

int vformatear(char* buf, size_t max, const char* fmt, va_list ap) {
    size_t n = 0;
#define PON(ch) do { if (n + 1 < max) buf[n] = (ch); n++; } while (0)
    for (; *fmt; fmt++) {
        if (*fmt != '%') { PON(*fmt); continue; }
        fmt++;
        char relleno = ' ';
        int ancho = 0, largo = 0, izquierda = 0;
        for (;; fmt++) {
            if (*fmt == '0') relleno = '0';
            else if (*fmt == '-') izquierda = 1;
            else break;
        }
        if (izquierda) relleno = ' ';
        while (*fmt >= '0' && *fmt <= '9') ancho = ancho * 10 + (*fmt++ - '0');
        while (*fmt == 'l') { largo++; fmt++; }
        char tmp[24];
        const char* s = tmp;
        int len = 0, neg = 0;
        switch (*fmt) {
        case 'd': case 'i': case 'u': case 'x': case 'X': {
            uint64_t v;
            if (*fmt == 'd' || *fmt == 'i') {
                int64_t sv = largo ? va_arg(ap, int64_t) : va_arg(ap, int);
                if (sv < 0) { neg = 1; v = (uint64_t)(-(sv + 1)) + 1; } else v = (uint64_t)sv;
            } else {
                v = largo ? va_arg(ap, uint64_t) : va_arg(ap, unsigned);
            }
            unsigned base = (*fmt == 'x' || *fmt == 'X') ? 16 : 10;
            const char* dig = *fmt == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
            char inv[24];
            int k = 0;
            do { inv[k++] = dig[v % base]; v /= base; } while (v);
            if (neg) tmp[len++] = '-';
            while (k) tmp[len++] = inv[--k];
            break;
        }
        case 'c': tmp[0] = (char)va_arg(ap, int); len = 1; break;
        case 's': s = va_arg(ap, const char*); if (!s) s = "(nulo)"; len = (int)strlen(s); break;
        case '%': tmp[0] = '%'; len = 1; break;
        case 0: fmt--; continue;
        default: tmp[0] = '%'; tmp[1] = *fmt; len = 2; break;
        }
        int pad = ancho - len;
        if (relleno == '0' && neg && pad > 0) { PON('-'); s++; len--; }
        if (!izquierda) while (pad-- > 0) PON(relleno);
        for (int i = 0; i < len; i++) PON(s[i]);
        if (izquierda) while (pad-- > 0) PON(' ');
    }
    if (max) buf[n < max ? n : max - 1] = 0;
#undef PON
    return (int)n;
}
int formatear(char* buf, size_t max, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vformatear(buf, max, fmt, ap);
    va_end(ap);
    return r;
}

/* ---------------------------------------------------------------- archivos */
int archivo_tamano(const char* ruta) { return (int)L1(SYS_ARCHIVO_TAMANO, ruta); }
int archivo_leer(const char* ruta, void* buf, int max) { return (int)L3(SYS_ARCHIVO_LEER, ruta, buf, max); }
int archivo_escribir(const char* ruta, const void* datos, int bytes) {
    return (int)L3(SYS_ARCHIVO_ESCRIBIR, ruta, datos, bytes);
}

/* ---------------------------------------------------------------- ventanas */
int ventana_crear(const char* titulo, int ancho, int alto, int estilo) {
    return (int)L4(SYS_VENTANA_CREAR, titulo, ancho, alto, estilo);
}
void ventana_titulo(int ventana, const char* titulo) { L2(SYS_VENTANA_TITULO, ventana, titulo); }
void ventana_cerrar(int ventana) { L1(SYS_VENTANA_CERRAR, ventana); }

static int control(int ventana, int tipo, int x, int y, int ancho, int alto, const char* texto) {
    Rect32 r = {x, y, ancho, alto};
    return (int)L4(SYS_CONTROL_CREAR, ventana, tipo, &r, texto ? texto : "");
}
int boton(int v, int x, int y, int an, int al, const char* t) { return control(v, CONTROL_BOTON, x, y, an, al, t); }
int etiqueta(int v, int x, int y, int an, int al, const char* t) { return control(v, CONTROL_ETIQUETA, x, y, an, al, t); }
int campo_texto(int v, int x, int y, int an, int al, const char* t) { return control(v, CONTROL_TEXTO, x, y, an, al, t); }
int casilla(int v, int x, int y, int an, int al, const char* t) { return control(v, CONTROL_CASILLA, x, y, an, al, t); }
void control_texto(int c, const char* texto) { L2(SYS_CONTROL_TEXTO, c, texto); }
int control_leer(int c, char* buf, int max) { return (int)L3(SYS_CONTROL_LEER, c, buf, max); }
int casilla_marcada(int c) { return (int)L2(SYS_CONTROL_VALOR, c, -1); }
void casilla_marcar(int c, int marcada) { L2(SYS_CONTROL_VALOR, c, marcada ? 1 : 0); }

/* ---------------------------------------------------------------- lienzo */
Lienzo* lienzo_crear(int ventana, int x, int y, int ancho, int alto) {
    Rect32 r = {x, y, ancho, alto};
    uint32_t* px = (uint32_t*)L2(SYS_LIENZO_CREAR, ventana, &r);
    if (!px) return 0;
    Lienzo* l = malloc(sizeof(Lienzo));
    if (!l) return 0;
    l->ventana = ventana;
    l->px = px;
    l->ancho = ancho;
    l->alto = alto;
    return l;
}
void lienzo_mostrar(Lienzo* l) { L1(SYS_LIENZO_MOSTRAR, l->ventana); }
void lienzo_limpiar(Lienzo* l, uint32_t color) { lienzo_rect(l, 0, 0, l->ancho, l->alto, color); }
void lienzo_pixel(Lienzo* l, int x, int y, uint32_t color) {
    if ((unsigned)x < (unsigned)l->ancho && (unsigned)y < (unsigned)l->alto) l->px[y * l->ancho + x] = color;
}
void lienzo_rect(Lienzo* l, int x, int y, int an, int al, uint32_t color) {
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + an > l->ancho ? l->ancho : x + an, y1 = y + al > l->alto ? l->alto : y + al;
    for (int j = y0; j < y1; j++) {
        uint32_t* f = l->px + j * l->ancho;
        for (int i = x0; i < x1; i++) f[i] = color;
    }
}
void lienzo_marco(Lienzo* l, int x, int y, int an, int al, uint32_t color) {
    lienzo_rect(l, x, y, an, 1, color);
    lienzo_rect(l, x, y + al - 1, an, 1, color);
    lienzo_rect(l, x, y, 1, al, color);
    lienzo_rect(l, x + an - 1, y, 1, al, color);
}
void lienzo_linea(Lienzo* l, int x0, int y0, int x1, int y1, uint32_t color) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        lienzo_pixel(l, x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}
void lienzo_circulo(Lienzo* l, int cx, int cy, int r, uint32_t color, int relleno) {
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        if (relleno) {
            lienzo_rect(l, cx - x, cy + y, 2 * x + 1, 1, color);
            lienzo_rect(l, cx - x, cy - y, 2 * x + 1, 1, color);
            lienzo_rect(l, cx - y, cy + x, 2 * y + 1, 1, color);
            lienzo_rect(l, cx - y, cy - x, 2 * y + 1, 1, color);
        } else {
            lienzo_pixel(l, cx + x, cy + y, color); lienzo_pixel(l, cx - x, cy + y, color);
            lienzo_pixel(l, cx + x, cy - y, color); lienzo_pixel(l, cx - x, cy - y, color);
            lienzo_pixel(l, cx + y, cy + x, color); lienzo_pixel(l, cx - y, cy + x, color);
            lienzo_pixel(l, cx + y, cy - x, color); lienzo_pixel(l, cx - y, cy - x, color);
        }
        y++;
        if (err < 0) err += 2 * y + 1;
        else { x--; err += 2 * (y - x) + 1; }
    }
}
int lienzo_texto(Lienzo* l, int x, int y, const char* texto, uint32_t color) {
    return (int)L5(SYS_LIENZO_TEXTO, l->ventana, x, y, texto, color);
}
void lienzo_imagen(Lienzo* l, int x, int y, const uint32_t* px, int an, int al) {
    for (int j = 0; j < al; j++) {
        int yy = y + j;
        if (yy < 0 || yy >= l->alto) continue;
        for (int i = 0; i < an; i++) {
            int xx = x + i;
            if (xx < 0 || xx >= l->ancho) continue;
            uint32_t c = px[j * an + i];
            uint32_t a = c >> 24;
            if (a == 255) l->px[yy * l->ancho + xx] = c & 0xFFFFFF;
            else if (a) {
                uint32_t d = l->px[yy * l->ancho + xx];
                uint32_t rb = ((c & 0xFF00FF) * a + (d & 0xFF00FF) * (255 - a)) >> 8;
                uint32_t g = ((c & 0xFF00) * a + (d & 0xFF00) * (255 - a)) >> 8;
                l->px[yy * l->ancho + xx] = (rb & 0xFF00FF) | (g & 0xFF00);
            }
        }
    }
}
int texto_ancho(const char* texto) { return (int)L1(SYS_TEXTO_ANCHO, texto); }

/* ---------------------------------------------------------------- eventos */
void temporizador(int ventana, int ms) { L2(SYS_TEMPORIZADOR, ventana, ms); }
int evento_esperar(Evento* e) { return L2(SYS_EVENTO, e, 1) == 1; }
int evento_consultar(Evento* e) { return L2(SYS_EVENTO, e, 0) == 1; }
int mensaje(int ventana, const char* titulo, const char* texto, int botones) {
    return (int)L4(SYS_MENSAJE, ventana, titulo, texto, botones);
}

/* ---------------------------------------------------------------- bibliotecas */
const char* traducir(const char* texto) {
    /* cada texto traducido se guarda en una pequeña tabla para no repetir la llamada */
    enum { N = 64 };
    static const char* claves[N];
    static char* valores[N];
    static int usados;
    for (int i = 0; i < usados; i++)
        if (claves[i] == texto) return valores[i];
    int len = (int)L3(SYS_TRADUCIR, texto, 0, 0);
    if (len < 0) return texto;
    char* v = malloc((size_t)len + 1);
    if (!v) return texto;
    L3(SYS_TRADUCIR, texto, v, len + 1);
    if (usados < N) {
        claves[usados] = texto;
        valores[usados++] = v;
    }
    return v;
}

uint32_t* imagen_cargar(const char* ruta, int* ancho, int* alto) {
    int32_t w = 0, h = 0;
    if (L5(SYS_IMAGEN, ruta, 0, 0, &w, &h) < 0 || w <= 0 || h <= 0) return 0;
    uint32_t* px = malloc((size_t)w * h * 4);
    if (!px) return 0;
    if (L5(SYS_IMAGEN, ruta, px, (uint64_t)w * h, &w, &h) < 0) { free(px); return 0; }
    if (ancho) *ancho = w;
    if (alto) *alto = h;
    return px;
}

/* Sockets de BrysOS (TCP y UDP sobre IPv4) para los programas de Linux: la interfaz de
 * Berkeley (connect, send, recv, poll...) construida sobre la API "raw" de lwIP. Los datos
 * recibidos esperan en una cadena de pbufs hasta que el programa los lee; las esperas ceden el
 * procesador para que el hilo de la red los vaya trayendo. Los errores son los de Linux. */
#include "lwip/tcp.h"
#include "lwip/udp.h"
#include "lwip/ip_addr.h"
#include "lwip/pbuf.h"
#include <brys/glue.h>
#include <brys/linux.h>
#include "stack.h"
#include <string.h>

void* memset(void* d, int c, size_t n);

enum {
    E_AGAIN = 11, E_NOMEM = 12, E_INVAL = 22, E_PIPE = 32, E_DESTADDRREQ = 89, E_MSGSIZE = 90,
    E_PROTONOSUPPORT = 93, E_OPNOTSUPP = 95, E_AFNOSUPPORT = 97, E_ADDRINUSE = 98, E_CONNRESET = 104,
    E_ISCONN = 106, E_NOTCONN = 107, E_TIMEDOUT = 110, E_CONNREFUSED = 111, E_HOSTUNREACH = 113,
    E_ALREADY = 114, E_INPROGRESS = 115,
};

#define UDP_QUEUE 32
#define ACCEPT_QUEUE 16

struct dgram {
    struct pbuf* p;
    ip_addr_t addr;
    u16_t port;
};

struct bsock {
    int type;                       /* 1 TCP, 2 UDP */
    struct tcp_pcb* tcp;
    struct udp_pcb* udp;
    struct pbuf* rx;                /* TCP: datos recibidos sin leer */
    u16_t rx_off;
    struct dgram q[UDP_QUEUE];      /* UDP: datagramas sin leer */
    int qh, qt;
    struct bsock* acc[ACCEPT_QUEUE];/* TCP en escucha: conexiones aceptadas */
    int na;
    int connecting, connected, listening, peer_closed, shut_wr;
    int error;                      /* error pendiente (SO_ERROR), positivo */
    int nodelay;
};

static int map_err(err_t e) {
    switch (e) {
    case ERR_OK: return 0;
    case ERR_MEM: case ERR_BUF: return E_NOMEM;
    case ERR_TIMEOUT: return E_TIMEDOUT;
    case ERR_RTE: return E_HOSTUNREACH;
    case ERR_INPROGRESS: return E_INPROGRESS;
    case ERR_USE: return E_ADDRINUSE;
    case ERR_ALREADY: return E_ALREADY;
    case ERR_ISCONN: return E_ISCONN;
    case ERR_CONN: return E_NOTCONN;
    case ERR_ABRT: case ERR_RST: return E_CONNRESET;
    case ERR_CLSD: return E_NOTCONN;
    default: return E_INVAL;
    }
}

/* --- direcciones (struct sockaddr_in de Linux) */
static int parse_addr(const void* sa, unsigned len, ip_addr_t* ip, u16_t* port) {
    const u8_t* b = sa;
    if (!sa || len < 8) return -E_INVAL;
    if (b[0] != 2 || b[1] != 0) return -E_AFNOSUPPORT;   /* AF_INET */
    *port = (u16_t)((b[2] << 8) | b[3]);
    u32_t a;
    memcpy(&a, b + 4, 4);
    ip_addr_set_ip4_u32(ip, a);
    return 0;
}

static int put_addr(void* sa, unsigned cap, const ip_addr_t* ip, u16_t port) {
    u8_t b[16] = {0};
    b[0] = 2;
    b[2] = (u8_t)(port >> 8);
    b[3] = (u8_t)port;
    u32_t a = ip4_addr_get_u32(ip_2_ip4(ip));
    memcpy(b + 4, &a, 4);
    if (sa) memcpy(sa, b, cap < 16 ? cap : 16);
    return 16;
}

/* --- espera cooperativa: el hilo de la red avanza mientras tanto */
static void wait_a_bit(void) { brys_sleep_ms(2); }

/* --- TCP: funciones que llama lwIP (hilo "linux-trabajos") */
static err_t tcp_recv_cb(void* arg, struct tcp_pcb* pcb, struct pbuf* p, err_t e) {
    struct bsock* s = arg;
    (void)pcb;
    if (!s) { if (p) pbuf_free(p); return ERR_OK; }
    if (!p) { s->peer_closed = 1; return ERR_OK; }
    if (e != ERR_OK) { pbuf_free(p); return e; }
    if (s->rx) pbuf_cat(s->rx, p);
    else s->rx = p;
    return ERR_OK;
}

static void tcp_err_cb(void* arg, err_t e) {
    struct bsock* s = arg;
    if (!s) return;
    s->tcp = NULL;   /* lwIP ya lo ha liberado */
    s->error = s->connecting ? (e == ERR_RST ? E_CONNREFUSED : map_err(e)) : map_err(e);
    s->connecting = 0;
    s->peer_closed = 1;
}

static err_t tcp_connected_cb(void* arg, struct tcp_pcb* pcb, err_t e) {
    struct bsock* s = arg;
    (void)pcb;
    s->connecting = 0;
    if (e == ERR_OK) s->connected = 1;
    else s->error = map_err(e);
    return ERR_OK;
}

static void setup_tcp(struct bsock* s, struct tcp_pcb* pcb) {
    s->tcp = pcb;
    tcp_arg(pcb, s);
    tcp_recv(pcb, tcp_recv_cb);
    tcp_err(pcb, tcp_err_cb);
}

static err_t tcp_accept_cb(void* arg, struct tcp_pcb* npcb, err_t e) {
    struct bsock* s = arg;
    if (e != ERR_OK || !npcb) return ERR_VAL;
    if (s->na >= ACCEPT_QUEUE) { tcp_abort(npcb); return ERR_ABRT; }
    struct bsock* n = brys_kmalloc(sizeof *n);
    if (!n) { tcp_abort(npcb); return ERR_ABRT; }
    memset(n, 0, sizeof *n);
    n->type = 1;
    n->connected = 1;
    setup_tcp(n, npcb);
    s->acc[s->na++] = n;
    return ERR_OK;
}

/* --- UDP */
static void udp_recv_cb(void* arg, struct udp_pcb* pcb, struct pbuf* p, const ip_addr_t* addr, u16_t port) {
    struct bsock* s = arg;
    (void)pcb;
    int next = (s->qt + 1) % UDP_QUEUE;
    if (next == s->qh) { pbuf_free(p); return; }   /* cola llena: se pierde, como en UDP */
    s->q[s->qt].p = p;
    s->q[s->qt].addr = *addr;
    s->q[s->qt].port = port;
    s->qt = next;
}

/* ------------------------------------------------------------------------------------
 * Interfaz (net/stack.h)
 * ------------------------------------------------------------------------------------ */
int brys_socket_create(int domain, int type, int proto, void** out) {
    if (domain != 2) return -E_AFNOSUPPORT;   /* sólo IPv4 (BrysOS no tiene IPv6 todavía) */
    if (type != 1 && type != 2) return -E_PROTONOSUPPORT;
    (void)proto;
    /* la red arranca con el primer socket; se espera (hasta 10 s) a tener dirección por DHCP */
    brys_linux_start();
    brys_stack_start();
    for (uint64_t end = brys_ticks_ms() + 10000; !brys_stack_has_ip() && brys_ticks_ms() < end;) brys_sleep_ms(20);
    struct bsock* s = brys_kmalloc(sizeof *s);
    if (!s) return -E_NOMEM;
    memset(s, 0, sizeof *s);
    s->type = type;
    if (type == 1) {
        struct tcp_pcb* pcb = tcp_new();
        if (!pcb) { brys_kfree(s); return -E_NOMEM; }
        setup_tcp(s, pcb);
    } else {
        s->udp = udp_new();
        if (!s->udp) { brys_kfree(s); return -E_NOMEM; }
        udp_recv(s->udp, udp_recv_cb, s);
    }
    *out = s;
    return 0;
}

void brys_socket_close(void* sock) {
    struct bsock* s = sock;
    if (s->tcp) {
        tcp_arg(s->tcp, NULL);
        tcp_recv(s->tcp, NULL);
        tcp_err(s->tcp, NULL);
        tcp_accept(s->tcp, NULL);
        if (tcp_close(s->tcp) != ERR_OK) tcp_abort(s->tcp);
    }
    if (s->udp) udp_remove(s->udp);
    if (s->rx) pbuf_free(s->rx);
    while (s->qh != s->qt) { pbuf_free(s->q[s->qh].p); s->qh = (s->qh + 1) % UDP_QUEUE; }
    for (int i = 0; i < s->na; i++) brys_socket_close(s->acc[i]);
    brys_kfree(s);
}

int brys_socket_bind(void* sock, const void* addr, unsigned len) {
    struct bsock* s = sock;
    ip_addr_t ip;
    u16_t port;
    int r = parse_addr(addr, len, &ip, &port);
    if (r) return r;
    err_t e = s->type == 1 ? (s->tcp ? tcp_bind(s->tcp, &ip, port) : ERR_CLSD) : udp_bind(s->udp, &ip, port);
    return -map_err(e);
}

int brys_socket_listen(void* sock, int backlog) {
    struct bsock* s = sock;
    (void)backlog;
    if (s->type != 1 || !s->tcp) return -E_OPNOTSUPP;
    struct tcp_pcb* l = tcp_listen(s->tcp);
    if (!l) return -E_NOMEM;
    s->tcp = l;
    s->listening = 1;
    tcp_arg(l, s);
    tcp_accept(l, tcp_accept_cb);
    return 0;
}

int brys_socket_accept(void* sock, void** out, int nonblock) {
    struct bsock* s = sock;
    if (!s->listening) return -E_INVAL;
    while (!s->na) {
        if (nonblock) return -E_AGAIN;
        wait_a_bit();
    }
    *out = s->acc[0];
    for (int i = 1; i < s->na; i++) s->acc[i - 1] = s->acc[i];
    s->na--;
    return 0;
}

int brys_socket_connect(void* sock, const void* addr, unsigned len, int nonblock) {
    struct bsock* s = sock;
    ip_addr_t ip;
    u16_t port;
    int r = parse_addr(addr, len, &ip, &port);
    if (r) return r;
    if (s->type == 2) return -map_err(udp_connect(s->udp, &ip, port));
    if (s->connected) return -E_ISCONN;
    if (s->connecting) return nonblock ? -E_ALREADY : -E_INPROGRESS;
    if (!s->tcp) return -(s->error ? s->error : E_CONNRESET);
    s->connecting = 1;
    err_t e = tcp_connect(s->tcp, &ip, port, tcp_connected_cb);
    if (e != ERR_OK) { s->connecting = 0; return -map_err(e); }
    if (nonblock) return -E_INPROGRESS;
    uint64_t end = brys_ticks_ms() + 30000;
    while (s->connecting && brys_ticks_ms() < end) wait_a_bit();
    if (s->connected) return 0;
    if (s->connecting) { s->connecting = 0; return -E_TIMEDOUT; }
    return -(s->error ? s->error : E_CONNREFUSED);
}

long brys_socket_sendto(void* sock, const void* buf, uint64_t len, const void* addr, unsigned alen, int nonblock) {
    struct bsock* s = sock;
    if (s->type == 2) {
        ip_addr_t ip;
        u16_t port = 0;
        if (addr) {
            int r = parse_addr(addr, alen, &ip, &port);
            if (r) return r;
        } else if (!(s->udp->flags & UDP_FLAGS_CONNECTED)) {
            return -E_DESTADDRREQ;
        }
        if (len > 65000) return -E_MSGSIZE;
        struct pbuf* p = pbuf_alloc(PBUF_TRANSPORT, (u16_t)len, PBUF_RAM);
        if (!p) return -E_NOMEM;
        memcpy(p->payload, buf, len);
        err_t e = addr ? udp_sendto(s->udp, p, &ip, port) : udp_send(s->udp, p);
        pbuf_free(p);
        return e == ERR_OK ? (long)len : -map_err(e);
    }
    if (s->error) return -s->error;
    if (!s->connected || !s->tcp) return s->peer_closed ? -E_PIPE : -E_NOTCONN;
    if (s->shut_wr) return -E_PIPE;
    const u8_t* d = buf;
    uint64_t sent = 0;
    while (sent < len) {
        if (!s->tcp) return sent ? (long)sent : -(s->error ? s->error : E_PIPE);
        u16_t room = tcp_sndbuf(s->tcp);
        if (!room) {
            tcp_output(s->tcp);
            if (nonblock) return sent ? (long)sent : -E_AGAIN;
            wait_a_bit();
            continue;
        }
        u16_t n = (u16_t)(len - sent < room ? len - sent : room);
        err_t e = tcp_write(s->tcp, d + sent, n, TCP_WRITE_FLAG_COPY | (sent + n < len ? TCP_WRITE_FLAG_MORE : 0));
        if (e == ERR_MEM) {
            tcp_output(s->tcp);
            if (nonblock) return sent ? (long)sent : -E_AGAIN;
            wait_a_bit();
            continue;
        }
        if (e != ERR_OK) return sent ? (long)sent : -map_err(e);
        sent += n;
    }
    tcp_output(s->tcp);
    return (long)sent;
}

long brys_socket_send(void* sock, const void* buf, uint64_t len, int nonblock) {
    return brys_socket_sendto(sock, buf, len, NULL, 0, nonblock);
}

long brys_socket_recvfrom(void* sock, void* buf, uint64_t len, int nonblock, void* addr, uint32_t* alen) {
    struct bsock* s = sock;
    if (s->type == 2) {
        while (s->qh == s->qt) {
            if (nonblock) return -E_AGAIN;
            wait_a_bit();
        }
        struct dgram* g = &s->q[s->qh];
        u16_t n = (u16_t)(g->p->tot_len < len ? g->p->tot_len : len);
        pbuf_copy_partial(g->p, buf, n, 0);
        if (addr && alen) *alen = (uint32_t)put_addr(addr, *alen, &g->addr, g->port);
        pbuf_free(g->p);
        s->qh = (s->qh + 1) % UDP_QUEUE;
        return n;
    }
    while (!s->rx) {
        if (s->peer_closed) return s->error ? -s->error : 0;
        if (!s->connected && !s->connecting) return -E_NOTCONN;
        if (nonblock) return -E_AGAIN;
        wait_a_bit();
    }
    u8_t* out = buf;
    uint64_t got = 0;
    while (s->rx && got < len) {
        struct pbuf* p = s->rx;
        u16_t avail = (u16_t)(p->len - s->rx_off);
        u16_t n = (u16_t)(len - got < avail ? len - got : avail);
        memcpy(out + got, (u8_t*)p->payload + s->rx_off, n);
        got += n;
        s->rx_off = (u16_t)(s->rx_off + n);
        if (s->rx_off == p->len) {
            s->rx = p->next;
            if (s->rx) pbuf_ref(s->rx);
            pbuf_free(p);   /* sólo este eslabón: el resto sigue referenciado */
            s->rx_off = 0;
        }
    }
    for (uint64_t left = got; s->tcp && left;) {   /* se abre la ventana de recepción */
        u16_t k = (u16_t)(left > 0xFFFF ? 0xFFFF : left);
        tcp_recved(s->tcp, k);
        left -= k;
    }
    if (addr && alen && s->tcp) *alen = (uint32_t)put_addr(addr, *alen, &s->tcp->remote_ip, s->tcp->remote_port);
    return (long)got;
}

long brys_socket_recv(void* sock, void* buf, uint64_t len, int nonblock) {
    return brys_socket_recvfrom(sock, buf, len, nonblock, NULL, NULL);
}

int brys_socket_poll(void* sock) {
    struct bsock* s = sock;
    int ev = 0;
    if (s->type == 2) {
        if (s->qh != s->qt) ev |= 0x1;
        ev |= 0x4;
        return ev;
    }
    if (s->listening) return s->na ? 0x1 : 0;
    if (s->rx || s->peer_closed) ev |= 0x1;
    if (s->connected && s->tcp && tcp_sndbuf(s->tcp)) ev |= 0x4;
    if (s->error) ev |= 0x8;
    if (s->peer_closed && !s->rx) ev |= 0x10;
    return ev;
}

int brys_socket_available(void* sock) {
    struct bsock* s = sock;
    if (s->type == 2) return s->qh != s->qt ? s->q[s->qh].p->tot_len : 0;
    return s->rx ? s->rx->tot_len - s->rx_off : 0;
}

int brys_socket_shutdown(void* sock, int how) {
    struct bsock* s = sock;
    if (s->type != 1 || !s->tcp) return -E_NOTCONN;
    if (how == 1 || how == 2) {
        s->shut_wr = 1;
        tcp_shutdown(s->tcp, 0, 1);
    }
    return 0;
}

int brys_socket_name(void* sock, int peer, void* addr, unsigned cap) {
    struct bsock* s = sock;
    if (s->type == 1) {
        if (!s->tcp) return -E_NOTCONN;
        return put_addr(addr, cap, peer ? &s->tcp->remote_ip : &s->tcp->local_ip, peer ? s->tcp->remote_port : s->tcp->local_port);
    }
    if (peer && !(s->udp->flags & UDP_FLAGS_CONNECTED)) return -E_NOTCONN;
    return put_addr(addr, cap, peer ? &s->udp->remote_ip : &s->udp->local_ip, peer ? s->udp->remote_port : s->udp->local_port);
}

int brys_socket_setopt(void* sock, int level, int name, const void* val, unsigned len) {
    struct bsock* s = sock;
    if (level == 6 && name == 1 && val && len >= 4 && s->tcp) {   /* IPPROTO_TCP, TCP_NODELAY */
        if (*(const int*)val) tcp_nagle_disable(s->tcp);
        else tcp_nagle_enable(s->tcp);
        return 0;
    }
    return 0;   /* el resto (SO_REUSEADDR, SO_KEEPALIVE, búferes, tiempos...) se acepta sin más */
}

int brys_socket_getopt(void* sock, int level, int name, void* val, unsigned* len) {
    struct bsock* s = sock;
    int v = 0;
    if (level == 1 && name == 4) {   /* SOL_SOCKET, SO_ERROR */
        v = s->error;
        s->error = 0;
    } else if (level == 1 && name == 3) {   /* SO_TYPE */
        v = s->type;
    } else if (level == 1 && (name == 7 || name == 8)) {   /* SO_SNDBUF, SO_RCVBUF */
        v = TCP_WND;
    }
    if (*len >= 4) { memcpy(val, &v, 4); *len = 4; }
    return 0;
}

/* Nota al terminar este archivo:
 * Berkeley sockets sobre la API raw de lwIP es como poner un mostrador de banco delante de un
 * almacén donde todo pasa por cintas transportadoras: el cliente espera en la ventanilla
 * (wait_a_bit) mientras las cintas (el hilo de la red) traen los paquetes. curl no notará la
 * diferencia; esa es la gracia. — Claude, octubre de 2026 */

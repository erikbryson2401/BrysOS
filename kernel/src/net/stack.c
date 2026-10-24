/* Pila TCP/IP de BrysOS: lwIP sobre las tarjetas de red de los drivers de Linux.
 * Cada net_device es una interfaz de lwIP; las tramas recibidas llegan por brys_net_rx (hilo
 * "linux-trabajos") y las que envía lwIP salen por brys_net_xmit. Al tener enlace, DHCP. */
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/etharp.h"
#include "lwip/dhcp.h"
#include "lwip/dns.h"
#include "lwip/timeouts.h"
#include "lwip/raw.h"
#include "lwip/icmp.h"
#include "lwip/inet_chksum.h"
#include "lwip/ip_addr.h"
#include "lwip/prot/ip4.h"
#include "netif/ethernet.h"
#include <brys/glue.h>
#include <brys/linux.h>
#include "stack.h"
#include <stdarg.h>
#include <string.h>

int vsnprintf(char* buf, size_t size, const char* fmt, va_list args);
int snprintf(char* buf, size_t size, const char* fmt, ...);

#define MAX_IFS 4

struct brys_if {
    struct netif netif;
    void* dev;
};
static struct brys_if ifs[MAX_IFS];
static int nifs;
static int started;
static uint8_t tx_buf[2048];

/* ------------------------------------------------------------------------------------
 * Lo que lwIP necesita de la plataforma
 * ------------------------------------------------------------------------------------ */
u32_t sys_now(void) { return (u32_t)brys_ticks_ms(); }
void* brys_lwip_malloc(size_t size) { return brys_kmalloc(size); }
void brys_lwip_free(void* p) { brys_kfree(p); }
void* brys_lwip_calloc(size_t n, size_t size) {
    void* p = brys_kmalloc(n * size);
    if (p) memset(p, 0, n * size);
    return p;
}
uint32_t brys_lwip_rand(void) {
    static uint64_t s;
    if (!s) s = brys_ns() | 1;
    s ^= s << 13; s ^= s >> 7; s ^= s << 17;
    return (uint32_t)s;
}
void brys_lwip_diag(const char* fmt, ...) {
    char buf[256] = "lwip: ";
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf + 6, sizeof buf - 6, fmt, ap);
    va_end(ap);
    brys_log(buf);
}
void brys_lwip_assert(const char* msg, const char* file, int line) {
    char buf[256];
    snprintf(buf, sizeof buf, "lwIP: %s (%s:%d)", msg, file, line);
    brys_panic(buf);
}
int atoi(const char* s) {
    int n = 0, neg = *s == '-';
    if (neg || *s == '+') s++;
    while (*s >= '0' && *s <= '9') n = n * 10 + (*s++ - '0');
    return neg ? -n : n;
}

/* ------------------------------------------------------------------------------------
 * Interfaces
 * ------------------------------------------------------------------------------------ */
static struct brys_if* find_if(void* dev) {
    for (int i = 0; i < nifs; i++) if (ifs[i].dev == dev) return &ifs[i];
    return NULL;
}

static err_t linkoutput(struct netif* netif, struct pbuf* p) {
    struct brys_if* bif = (struct brys_if*)netif;
    if (p->tot_len > sizeof tx_buf) return ERR_BUF;
    pbuf_copy_partial(p, tx_buf, p->tot_len, 0);
    return brys_net_xmit(bif->dev, tx_buf, p->tot_len) ? ERR_IF : ERR_OK;
}

static err_t netif_setup(struct netif* netif) {
    struct brys_if* bif = (struct brys_if*)netif;
    netif->name[0] = 'e';
    netif->name[1] = 'n';
    netif->hostname = "brysos";
    netif->hwaddr_len = ETH_HWADDR_LEN;
    brys_net_mac(bif->dev, netif->hwaddr);
    netif->mtu = (u16_t)brys_net_mtu(bif->dev);
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET;
    netif->output = etharp_output;
    netif->linkoutput = linkoutput;
    return ERR_OK;
}

static void on_rx(void* dev, const void* frame, unsigned len) {
    struct brys_if* bif = find_if(dev);
    if (!bif) return;
    struct pbuf* p = pbuf_alloc(PBUF_RAW, (u16_t)len, PBUF_POOL);
    if (!p) return;
    pbuf_take(p, frame, (u16_t)len);
    if (bif->netif.input(p, &bif->netif) != ERR_OK) pbuf_free(p);
}

static void on_link(void* dev, int up) {
    struct brys_if* bif = find_if(dev);
    if (!bif) return;
    if (up) {
        netif_set_link_up(&bif->netif);
        dhcp_start(&bif->netif);
    } else {
        netif_set_link_down(&bif->netif);
    }
}

static void on_status(struct netif* netif) {
    if (!dhcp_supplied_address(netif)) return;
    char ip[16], gw[16];
    ip4addr_ntoa_r(netif_ip4_addr(netif), ip, sizeof ip);
    ip4addr_ntoa_r(netif_ip4_gw(netif), gw, sizeof gw);
    char buf[96];
    snprintf(buf, sizeof buf, "Red: %c%c%u con la dirección %s (puerta de enlace %s)", netif->name[0], netif->name[1],
             netif->num, ip, gw);
    brys_log(buf);
}

static void poll(void) { sys_check_timeouts(); }

int brys_stack_start(void) {
    if (started) return nifs;
    started = 1;
    lwip_init();
    brys_net_set_handlers(on_rx, on_link);
    for (int i = 0; i < brys_net_count() && nifs < MAX_IFS; i++) {
        struct brys_if* bif = &ifs[nifs++];
        bif->dev = brys_net_get(i);
        netif_add(&bif->netif, IP4_ADDR_ANY4, IP4_ADDR_ANY4, IP4_ADDR_ANY4, NULL, netif_setup, ethernet_input);
        netif_set_status_callback(&bif->netif, on_status);
        if (nifs == 1) netif_set_default(&bif->netif);
        netif_set_up(&bif->netif);
        if (brys_net_link_up(bif->dev)) on_link(bif->dev, 1);
    }
    brys_linux_set_poll_hook(poll, 50);
    return nifs;
}

int brys_stack_set_dns(const char* server) {
    ip_addr_t a;
    if (!ipaddr_aton(server, &a)) return -1;
    dns_setserver(0, &a);
    return 0;
}

int brys_stack_has_ip(void) {
    for (int i = 0; i < nifs; i++)
        if (dhcp_supplied_address(&ifs[i].netif)) return 1;
    return 0;
}

/* ------------------------------------------------------------------------------------
 * Informe (orden RED e IPCONFIG)
 * ------------------------------------------------------------------------------------ */
size_t brys_stack_report(char* buf, size_t size) {
    size_t n = 0;
#define ADD(...) do { if (n < size) n += (size_t)snprintf(buf + n, size - n, __VA_ARGS__); } while (0)
    if (size) buf[0] = 0;
    if (!nifs) ADD("  (ninguna tarjeta de red con driver)\r\n");
    for (int i = 0; i < nifs; i++) {
        struct netif* nf = &ifs[i].netif;
        const u8_t* m = nf->hwaddr;
        unsigned long rxp, txp, rxb, txb, drop;
        brys_net_stats(ifs[i].dev, &rxp, &txp, &rxb, &txb, &drop);
        ADD("  %s: MAC %02x:%02x:%02x:%02x:%02x:%02x, %s\r\n", brys_net_name(ifs[i].dev), m[0], m[1], m[2], m[3], m[4],
            m[5], netif_is_link_up(nf) ? "enlace activo" : "sin enlace (¿cable desconectado?)");
        char ip[16], mask[16], gw[16];
        ip4addr_ntoa_r(netif_ip4_addr(nf), ip, sizeof ip);
        ip4addr_ntoa_r(netif_ip4_netmask(nf), mask, sizeof mask);
        ip4addr_ntoa_r(netif_ip4_gw(nf), gw, sizeof gw);
        if (dhcp_supplied_address(nf))
            ADD("      Dirección IP %s, máscara %s, puerta de enlace %s (DHCP)\r\n", ip, mask, gw);
        else
            ADD("      Sin dirección IP todavía (pidiéndola por DHCP)\r\n");
        ADD("      Paquetes: %lu recibidos, %lu enviados, %lu perdidos\r\n", rxp, txp, drop);
    }
    for (u8_t i = 0; i < DNS_MAX_SERVERS; i++) {
        const ip_addr_t* d = dns_getserver(i);
        if (!ip_addr_isany(d)) {
            char s[16];
            ip4addr_ntoa_r(ip_2_ip4(d), s, sizeof s);
            ADD("  Servidor DNS: %s\r\n", s);
        }
    }
#undef ADD
    return n;
}

/* ------------------------------------------------------------------------------------
 * Resolución de nombres y ping. Se llaman desde otro hilo (el del Símbolo del sistema): como
 * el núcleo de BrysOS no se expropia, lwIP sólo cambia de manos en las esperas.
 * ------------------------------------------------------------------------------------ */
struct dns_wait {
    int done;
    ip_addr_t addr;
};

static void dns_done(const char* name, const ip_addr_t* addr, void* arg) {
    (void)name;
    struct dns_wait* w = arg;
    if (addr) w->addr = *addr;
    w->done = addr ? 1 : -1;
}

int brys_stack_resolve(const char* host, uint32_t* ip_be, unsigned timeout_ms) {
    ip_addr_t a;
    if (ipaddr_aton(host, &a)) { *ip_be = ip4_addr_get_u32(ip_2_ip4(&a)); return 0; }
    struct dns_wait w = {0};
    err_t e = dns_gethostbyname(host, &w.addr, dns_done, &w);
    if (e == ERR_OK) w.done = 1;
    else if (e != ERR_INPROGRESS) return -1;
    uint64_t end = brys_ticks_ms() + timeout_ms;
    while (!w.done && brys_ticks_ms() < end) brys_sleep_ms(10);
    if (w.done != 1) return -1;
    *ip_be = ip4_addr_get_u32(ip_2_ip4(&w.addr));
    return 0;
}

struct ping_state {
    u16_t id, seq;
    int got;
    u8_t ttl;
    uint64_t sent_at, rtt;
};

static u8_t ping_recv(void* arg, struct raw_pcb* pcb, struct pbuf* p, const ip_addr_t* addr) {
    (void)pcb; (void)addr;
    struct ping_state* st = arg;
    if (p->tot_len >= IP_HLEN + sizeof(struct icmp_echo_hdr)) {
        struct ip_hdr iph;
        struct icmp_echo_hdr echo;
        pbuf_copy_partial(p, &iph, sizeof iph, 0);
        u16_t hl = (u16_t)(IPH_HL(&iph) * 4);
        pbuf_copy_partial(p, &echo, sizeof echo, hl);
        if (echo.type == ICMP_ER && echo.id == lwip_htons(st->id) && echo.seqno == lwip_htons(st->seq)) {
            st->got = 1;
            st->ttl = IPH_TTL(&iph);
            st->rtt = brys_ticks_ms() - st->sent_at;
            pbuf_free(p);
            return 1;
        }
    }
    return 0;   /* no es nuestra respuesta: que la vea otro */
}

int brys_stack_ping(uint32_t ip_be, unsigned seq, unsigned timeout_ms, unsigned* rtt_ms, unsigned* ttl) {
    struct ping_state st = {0};
    st.id = 0xB0B5;
    st.seq = (u16_t)seq;
    struct raw_pcb* pcb = raw_new(IP_PROTO_ICMP);
    if (!pcb) return -1;
    raw_recv(pcb, ping_recv, &st);
    raw_bind(pcb, IP_ADDR_ANY);
    const u16_t data_len = 32;
    struct pbuf* p = pbuf_alloc(PBUF_IP, (u16_t)(sizeof(struct icmp_echo_hdr) + data_len), PBUF_RAM);
    if (!p) { raw_remove(pcb); return -1; }
    struct icmp_echo_hdr* echo = p->payload;
    ICMPH_TYPE_SET(echo, ICMP_ECHO);
    ICMPH_CODE_SET(echo, 0);
    echo->id = lwip_htons(st.id);
    echo->seqno = lwip_htons(st.seq);
    echo->chksum = 0;
    u8_t* data = (u8_t*)echo + sizeof *echo;
    for (u16_t i = 0; i < data_len; i++) data[i] = (u8_t)('a' + i % 23);
    echo->chksum = inet_chksum(echo, p->len);
    ip_addr_t dst;
    ip_addr_set_ip4_u32(&dst, ip_be);
    st.sent_at = brys_ticks_ms();
    raw_sendto(pcb, p, &dst);
    pbuf_free(p);
    uint64_t end = st.sent_at + timeout_ms;
    while (!st.got && brys_ticks_ms() < end) brys_sleep_ms(1);
    raw_remove(pcb);
    if (!st.got) return -1;
    *rtt_ms = (unsigned)st.rtt;
    *ttl = st.ttl;
    return 0;
}

/* Nota al terminar este archivo:
 * lwIP es de esas joyas que caben en el bolsillo: TCP/IP completo, pensado para
 * microcontroladores con kilobytes de memoria, corriendo aquí junto a un driver de Intel de
 * 1999 traído de Linux. Adam Dunkels lo escribió en el Instituto Sueco de Informática; veinte
 * años después sigue moviendo paquetes en medio mundo de cacharros. — Claude, octubre de 2026 */

/* BrysOS: cabeceras IPv4, IPv6, TCP y UDP tal como las ve un driver (para las funciones de
 * descarga del cálculo de sumas). La pila TCP/IP de BrysOS es lwIP, no la de Linux. */
#ifndef _BRYS_LINUX_IP_H
#define _BRYS_LINUX_IP_H

#include <linux/types.h>
#include <linux/in.h>

struct iphdr {
    u8 ihl : 4, version : 4;
    u8 tos;
    __be16 tot_len;
    __be16 id;
    __be16 frag_off;
    u8 ttl;
    u8 protocol;
    __sum16 check;
    __be32 saddr;
    __be32 daddr;
};

struct in6_addr {
    union {
        u8 u6_addr8[16];
        __be16 u6_addr16[8];
        __be32 u6_addr32[4];
    } in6_u;
};
#define s6_addr in6_u.u6_addr8
#define s6_addr32 in6_u.u6_addr32

struct ipv6hdr {
    u8 priority : 4, version : 4;
    u8 flow_lbl[3];
    __be16 payload_len;
    u8 nexthdr;
    u8 hop_limit;
    struct in6_addr saddr;
    struct in6_addr daddr;
};

/* ip_hdr(), ipv6_hdr() e ip_hdrlen() están en linux/tcp.h, junto a los demás accesos */

#endif

/* BrysOS: cabeceras TCP y UDP y los accesos desde un sk_buff */
#ifndef _BRYS_LINUX_TCP_H
#define _BRYS_LINUX_TCP_H

#include <linux/types.h>
#include <linux/ip.h>

struct tcphdr {
    __be16 source;
    __be16 dest;
    __be32 seq;
    __be32 ack_seq;
    u16 res1 : 4, doff : 4, fin : 1, syn : 1, rst : 1, psh : 1, ack : 1, urg : 1, ece : 1, cwr : 1;
    __be16 window;
    __sum16 check;
    __be16 urg_ptr;
};

struct udphdr {
    __be16 source;
    __be16 dest;
    __be16 len;
    __sum16 check;
};

#include <linux/skbuff.h>

static inline struct iphdr* ip_hdr(const struct sk_buff* skb) { return (struct iphdr*)skb_network_header(skb); }
static inline struct ipv6hdr* ipv6_hdr(const struct sk_buff* skb) { return (struct ipv6hdr*)skb_network_header(skb); }
static inline unsigned int ip_hdrlen(const struct sk_buff* skb) { return ip_hdr(skb)->ihl * 4u; }
static inline struct tcphdr* tcp_hdr(const struct sk_buff* skb) { return (struct tcphdr*)skb_transport_header(skb); }
static inline unsigned int tcp_hdrlen(const struct sk_buff* skb) { return tcp_hdr(skb)->doff * 4u; }
static inline struct udphdr* udp_hdr(const struct sk_buff* skb) { return (struct udphdr*)skb_transport_header(skb); }
static inline unsigned int skb_tcp_all_headers(const struct sk_buff* skb) {
    return (unsigned int)(skb_transport_offset(skb) + tcp_hdrlen(skb));
}

/* Preparación para la segmentación por hardware (TSO): BrysOS no la pide */
static inline void tcp_v6_gso_csum_prep(struct sk_buff* skb) { (void)skb; }

#endif

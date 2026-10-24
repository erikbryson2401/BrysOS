/* BrysOS: paquetes de red de Linux (struct sk_buff). El búfer de cada paquete sale de páginas
 * físicas contiguas (brys/netdev.c), así que la tarjeta puede leerlo y escribirlo por DMA.
 * Diseño igual que en Linux: head <= data <= tail <= end, y al final del búfer la
 * skb_shared_info con los fragmentos. */
#ifndef _BRYS_LINUX_SKBUFF_H
#define _BRYS_LINUX_SKBUFF_H

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/dma-mapping.h>
#include <linux/atomic.h>
#include <linux/jiffies.h>

struct net_device;
struct sock;
struct napi_struct;

#define CHECKSUM_NONE 0
#define CHECKSUM_UNNECESSARY 1
#define CHECKSUM_COMPLETE 2
#define CHECKSUM_PARTIAL 3

#define NET_IP_ALIGN 2
#define NET_SKB_PAD 64
#define SMP_CACHE_BYTES 64
#define SKB_DATA_ALIGN(x) ALIGN((x), SMP_CACHE_BYTES)
#define MAX_SKB_FRAGS 17

#define PACKET_HOST 0
#define PACKET_BROADCAST 1
#define PACKET_MULTICAST 2
#define PACKET_OTHERHOST 3
#define PACKET_OUTGOING 4

typedef struct skb_frag {
    struct page* bv_page;
    unsigned int bv_len;
    unsigned int bv_offset;
} skb_frag_t;

struct skb_shared_info {
    u8 nr_frags;
    u8 tx_flags;
    unsigned short gso_size;
    unsigned short gso_segs;
    unsigned int gso_type;
    struct sk_buff* frag_list;
    skb_frag_t frags[MAX_SKB_FRAGS];
};
#define SKB_GSO_TCPV4 0x1
#define SKB_GSO_TCPV6 0x10
#define SKBTX_HW_TSTAMP 0x1
#define SKB_TRUESIZE(x) ((x) + SKB_DATA_ALIGN(sizeof(struct sk_buff)) + SKB_DATA_ALIGN(sizeof(struct skb_shared_info)))

struct sk_buff {
    struct sk_buff* next;
    struct sk_buff* prev;
    struct net_device* dev;
    struct sock* sk;
    ktime_t tstamp;
    char cb[48] __aligned(8);
    unsigned int len, data_len;
    u16 mac_len, hdr_len;
    u16 queue_mapping;
    u8 cloned : 1, ip_summed : 2, nohdr : 1, pkt_type : 3, no_fcs : 1;
    u8 peeked : 1, head_frag : 1, pfmemalloc : 1, encapsulation : 1, wifi_acked_valid : 1, wifi_acked : 1;
    u32 priority;
    int skb_iif;
    u32 hash;
    __be16 vlan_proto;
    u16 vlan_tci;
    union {
        __wsum csum;
        struct {
            u16 csum_start;
            u16 csum_offset;
        };
    };
    u32 mark;
    __be16 protocol;
    u16 transport_header;
    u16 network_header;
    u16 mac_header;
    unsigned char *head, *data;
    unsigned int tail, end;
    unsigned int truesize;
    refcount_t users;
    void (*destructor)(struct sk_buff* skb);
    unsigned int brys_bufsize;   /* BrysOS: tamaño del búfer de páginas */
};

struct sk_buff_head {
    struct sk_buff* next;
    struct sk_buff* prev;
    u32 qlen;
    spinlock_t lock;
};

/* Reserva y liberación (brys/netdev.c) */
struct sk_buff* alloc_skb(unsigned int size, gfp_t gfp);
struct sk_buff* __alloc_skb(unsigned int size, gfp_t gfp, int flags, int node);
struct sk_buff* build_skb(void* data, unsigned int frag_size);
#define napi_build_skb(data, sz) build_skb(data, sz)
struct sk_buff* __netdev_alloc_skb(struct net_device* dev, unsigned int len, gfp_t gfp);
#define netdev_alloc_skb(dev, len) __netdev_alloc_skb(dev, len, GFP_ATOMIC)
#define dev_alloc_skb(len) __netdev_alloc_skb(NULL, len, GFP_ATOMIC)
static inline struct sk_buff* netdev_alloc_skb_ip_align(struct net_device* dev, unsigned int len);
struct sk_buff* napi_alloc_skb(struct napi_struct* napi, unsigned int len);
void kfree_skb(struct sk_buff* skb);
void consume_skb(struct sk_buff* skb);
#define dev_kfree_skb(skb) consume_skb(skb)
#define dev_kfree_skb_any(skb) consume_skb(skb)
#define dev_kfree_skb_irq(skb) consume_skb(skb)
#define dev_consume_skb_any(skb) consume_skb(skb)
#define dev_consume_skb_irq(skb) consume_skb(skb)
#define napi_consume_skb(skb, budget) consume_skb(skb)
#define kfree_skb_reason(skb, r) kfree_skb(skb)
struct sk_buff* skb_copy(const struct sk_buff* skb, gfp_t gfp);
struct sk_buff* skb_clone(struct sk_buff* skb, gfp_t gfp);
struct sk_buff* skb_copy_expand(const struct sk_buff* skb, int newheadroom, int newtailroom, gfp_t gfp);
int pskb_expand_head(struct sk_buff* skb, int nhead, int ntail, gfp_t gfp);
void* netdev_alloc_frag(unsigned int fragsz);
#define napi_alloc_frag netdev_alloc_frag
void skb_free_frag(void* addr);
#define page_frag_free skb_free_frag
void skb_add_rx_frag(struct sk_buff* skb, int i, struct page* page, int off, int size, unsigned int truesize);

static inline struct skb_shared_info* skb_shinfo(const struct sk_buff* skb) {
    return (struct skb_shared_info*)(skb->head + skb->end);
}
static inline unsigned char* skb_tail_pointer(const struct sk_buff* skb) { return skb->head + skb->tail; }
static inline unsigned char* skb_end_pointer(const struct sk_buff* skb) { return skb->head + skb->end; }
static inline void skb_reset_tail_pointer(struct sk_buff* skb) { skb->tail = (unsigned int)(skb->data - skb->head); }
static inline void skb_set_tail_pointer(struct sk_buff* skb, int off) { skb_reset_tail_pointer(skb); skb->tail += off; }
static inline unsigned int skb_headlen(const struct sk_buff* skb) { return skb->len - skb->data_len; }
static inline bool skb_is_nonlinear(const struct sk_buff* skb) { return skb->data_len; }
static inline unsigned int skb_headroom(const struct sk_buff* skb) { return (unsigned int)(skb->data - skb->head); }
static inline int skb_tailroom(const struct sk_buff* skb) { return skb_is_nonlinear(skb) ? 0 : (int)(skb->end - skb->tail); }
static inline void skb_reserve(struct sk_buff* skb, int len) { skb->data += len; skb->tail += len; }
void* skb_put(struct sk_buff* skb, unsigned int len);
static inline void* __skb_put(struct sk_buff* skb, unsigned int len) {
    void* tmp = skb_tail_pointer(skb);
    skb->tail += len;
    skb->len += len;
    return tmp;
}
static inline void* skb_put_data(struct sk_buff* skb, const void* data, unsigned int len) {
    void* tmp = skb_put(skb, len);
    memcpy(tmp, data, len);
    return tmp;
}
static inline void* skb_put_zero(struct sk_buff* skb, unsigned int len) {
    void* tmp = skb_put(skb, len);
    memset(tmp, 0, len);
    return tmp;
}
static inline void skb_put_u8(struct sk_buff* skb, u8 val) { *(u8*)skb_put(skb, 1) = val; }
void* skb_push(struct sk_buff* skb, unsigned int len);
#define __skb_push skb_push
void* skb_pull(struct sk_buff* skb, unsigned int len);
#define __skb_pull skb_pull
static inline void* skb_pull_inline(struct sk_buff* skb, unsigned int len) { return skb_pull(skb, len); }
void skb_trim(struct sk_buff* skb, unsigned int len);
#define __skb_trim skb_trim
static inline int pskb_trim(struct sk_buff* skb, unsigned int len) { skb_trim(skb, len); return 0; }
void* __pskb_pull_tail(struct sk_buff* skb, int delta);
static inline bool pskb_may_pull(struct sk_buff* skb, unsigned int len) { return len <= skb_headlen(skb); }
static inline int skb_linearize(struct sk_buff* skb) { return skb_is_nonlinear(skb) ? -ENOMEM : 0; }
static inline int skb_cow_head(struct sk_buff* skb, unsigned int headroom) {
    return skb_headroom(skb) >= headroom ? 0 : pskb_expand_head(skb, (int)(headroom - skb_headroom(skb)), 0, GFP_ATOMIC);
}
static inline int skb_cow(struct sk_buff* skb, unsigned int headroom) { return skb_cow_head(skb, headroom); }
static inline int skb_padto(struct sk_buff* skb, unsigned int len) {
    if (skb->len >= len) return 0;
    if ((unsigned int)skb_tailroom(skb) < len - skb->len) { kfree_skb(skb); return -ENOMEM; }
    memset(skb->data + skb->len, 0, len - skb->len);
    return 0;
}
static inline int skb_put_padto(struct sk_buff* skb, unsigned int len) {
    if (skb->len >= len) return 0;
    if ((unsigned int)skb_tailroom(skb) < len - skb->len) { kfree_skb(skb); return -ENOMEM; }
    skb_put_zero(skb, len - skb->len);
    return 0;
}
static inline unsigned int skb_frag_size(const skb_frag_t* f) { return f->bv_len; }
static inline struct page* skb_frag_page(const skb_frag_t* f) { return f->bv_page; }
static inline unsigned int skb_frag_off(const skb_frag_t* f) { return f->bv_offset; }
static inline void* skb_frag_address(const skb_frag_t* f) { return (u8*)page_address(f->bv_page) + f->bv_offset; }
static inline void skb_fill_page_desc(struct sk_buff* skb, int i, struct page* page, int off, int size) {
    skb_frag_t* f = &skb_shinfo(skb)->frags[i];
    f->bv_page = page; f->bv_offset = (unsigned int)off; f->bv_len = (unsigned int)size;
    skb_shinfo(skb)->nr_frags = (u8)(i + 1);
}
static inline dma_addr_t skb_frag_dma_map(struct device* dev, const skb_frag_t* f, size_t off, size_t size,
                                          enum dma_data_direction dir) {
    return dma_map_page(dev, f->bv_page, f->bv_offset + off, size, dir);
}
static inline bool skb_is_gso(const struct sk_buff* skb) { return skb_shinfo(skb)->gso_size; }
static inline bool skb_is_gso_v6(const struct sk_buff* skb) { return skb_shinfo(skb)->gso_type & SKB_GSO_TCPV6; }
static inline unsigned char* skb_mac_header(const struct sk_buff* skb) { return skb->head + skb->mac_header; }
static inline unsigned char* skb_network_header(const struct sk_buff* skb) { return skb->head + skb->network_header; }
static inline unsigned char* skb_transport_header(const struct sk_buff* skb) { return skb->head + skb->transport_header; }
static inline void skb_reset_mac_header(struct sk_buff* skb) { skb->mac_header = (u16)(skb->data - skb->head); }
static inline void skb_reset_network_header(struct sk_buff* skb) { skb->network_header = (u16)(skb->data - skb->head); }
static inline void skb_reset_transport_header(struct sk_buff* skb) { skb->transport_header = (u16)(skb->data - skb->head); }
static inline void skb_set_mac_header(struct sk_buff* skb, int off) { skb_reset_mac_header(skb); skb->mac_header += off; }
static inline void skb_set_network_header(struct sk_buff* skb, int off) { skb_reset_network_header(skb); skb->network_header += off; }
static inline void skb_set_transport_header(struct sk_buff* skb, int off) { skb_reset_transport_header(skb); skb->transport_header += off; }
static inline int skb_network_offset(const struct sk_buff* skb) { return (int)(skb_network_header(skb) - skb->data); }
static inline int skb_transport_offset(const struct sk_buff* skb) { return (int)(skb_transport_header(skb) - skb->data); }
static inline int skb_checksum_start_offset(const struct sk_buff* skb) { return skb->csum_start - skb_headroom(skb); }
static inline void skb_checksum_none_assert(const struct sk_buff* skb) { (void)skb; }
static inline void skb_tx_timestamp(struct sk_buff* skb) { (void)skb; }
static inline void skb_record_rx_queue(struct sk_buff* skb, u16 q) { (void)skb; (void)q; }
static inline void skb_mark_for_recycle(struct sk_buff* skb) { (void)skb; }
static inline void skb_orphan(struct sk_buff* skb) { (void)skb; }
static inline u16 skb_get_queue_mapping(const struct sk_buff* skb) { return skb->queue_mapping; }
static inline void skb_set_queue_mapping(struct sk_buff* skb, u16 q) { skb->queue_mapping = q; }
static inline struct sk_buff* skb_get(struct sk_buff* skb) { skb->users.counter++; return skb; }
static inline bool skb_cloned(const struct sk_buff* skb) { return skb->cloned; }
static inline bool skb_shared(const struct sk_buff* skb) { return skb->users.counter != 1; }
static inline void skb_copy_from_linear_data(const struct sk_buff* skb, void* to, unsigned int len) { memcpy(to, skb->data, len); }
static inline void skb_copy_to_linear_data(struct sk_buff* skb, const void* from, unsigned int len) { memcpy(skb->data, from, len); }
int skb_copy_bits(const struct sk_buff* skb, int offset, void* to, int len);
static inline void* skb_header_pointer(const struct sk_buff* skb, int offset, int len, void* buffer) {
    if (offset + len <= (int)skb_headlen(skb)) return skb->data + offset;
    return skb_copy_bits(skb, offset, buffer, len) < 0 ? NULL : buffer;
}
static inline unsigned int skb_pagelen(const struct sk_buff* skb) { return skb_headlen(skb); }

/* Colas de paquetes */
static inline void __skb_queue_head_init(struct sk_buff_head* l) { l->prev = l->next = (struct sk_buff*)l; l->qlen = 0; }
static inline void skb_queue_head_init(struct sk_buff_head* l) { spin_lock_init(&l->lock); __skb_queue_head_init(l); }
static inline u32 skb_queue_len(const struct sk_buff_head* l) { return l->qlen; }
static inline bool skb_queue_empty(const struct sk_buff_head* l) { return l->next == (const struct sk_buff*)l; }
static inline struct sk_buff* skb_peek(const struct sk_buff_head* l) {
    struct sk_buff* s = l->next;
    return s == (const struct sk_buff*)l ? NULL : s;
}
static inline struct sk_buff* skb_peek_tail(const struct sk_buff_head* l) {
    struct sk_buff* s = l->prev;
    return s == (const struct sk_buff*)l ? NULL : s;
}
static inline void __skb_insert(struct sk_buff* n, struct sk_buff* prev, struct sk_buff* next, struct sk_buff_head* l) {
    n->next = next; n->prev = prev; next->prev = n; prev->next = n; l->qlen++;
}
static inline void __skb_queue_tail(struct sk_buff_head* l, struct sk_buff* n) { __skb_insert(n, l->prev, (struct sk_buff*)l, l); }
static inline void __skb_queue_head(struct sk_buff_head* l, struct sk_buff* n) { __skb_insert(n, (struct sk_buff*)l, l->next, l); }
static inline void __skb_unlink(struct sk_buff* skb, struct sk_buff_head* l) {
    l->qlen--;
    skb->next->prev = skb->prev; skb->prev->next = skb->next;
    skb->next = skb->prev = NULL;
}
static inline struct sk_buff* __skb_dequeue(struct sk_buff_head* l) {
    struct sk_buff* s = skb_peek(l);
    if (s) __skb_unlink(s, l);
    return s;
}
static inline struct sk_buff* __skb_dequeue_tail(struct sk_buff_head* l) {
    struct sk_buff* s = skb_peek_tail(l);
    if (s) __skb_unlink(s, l);
    return s;
}
void skb_queue_tail(struct sk_buff_head* l, struct sk_buff* n);
void skb_queue_head(struct sk_buff_head* l, struct sk_buff* n);
struct sk_buff* skb_dequeue(struct sk_buff_head* l);
struct sk_buff* skb_dequeue_tail(struct sk_buff_head* l);
void skb_unlink(struct sk_buff* skb, struct sk_buff_head* l);
void skb_queue_purge(struct sk_buff_head* l);
#define __skb_queue_purge skb_queue_purge
static inline void skb_queue_splice_tail_init(struct sk_buff_head* from, struct sk_buff_head* to) {
    struct sk_buff* s;
    while ((s = __skb_dequeue(from))) __skb_queue_tail(to, s);
}
#define skb_queue_splice_init skb_queue_splice_tail_init
#define skb_queue_walk(queue, skb) for (skb = (queue)->next; skb != (struct sk_buff*)(queue); skb = skb->next)
#define skb_queue_walk_safe(queue, skb, tmp) \
    for (skb = (queue)->next, tmp = skb->next; skb != (struct sk_buff*)(queue); skb = tmp, tmp = skb->next)
#define skb_list_walk_safe(first, skb, next_skb) \
    for ((skb) = (first), (next_skb) = (skb) ? (skb)->next : NULL; (skb); (skb) = (next_skb), (next_skb) = (skb) ? (skb)->next : NULL)

static inline struct sk_buff* netdev_alloc_skb_ip_align(struct net_device* dev, unsigned int len) {
    struct sk_buff* skb = netdev_alloc_skb(dev, len + NET_IP_ALIGN);
    if (skb) skb_reserve(skb, NET_IP_ALIGN);
    return skb;
}

#endif

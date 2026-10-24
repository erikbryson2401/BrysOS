/* BrysOS: utilidades de Ethernet (direcciones MAC, alloc_etherdev) */
#ifndef _BRYS_LINUX_ETHERDEVICE_H
#define _BRYS_LINUX_ETHERDEVICE_H

#include <linux/netdevice.h>
#include <linux/if_ether.h>

struct net_device* alloc_etherdev_mqs(int sizeof_priv, unsigned int txqs, unsigned int rxqs);
#define alloc_etherdev(sizeof_priv) alloc_etherdev_mqs(sizeof_priv, 1, 1)
#define alloc_etherdev_mq(sizeof_priv, count) alloc_etherdev_mqs(sizeof_priv, count, count)
#define devm_alloc_etherdev(dev, sizeof_priv) alloc_etherdev(sizeof_priv)

static inline bool is_zero_ether_addr(const u8* a) { return !(a[0] | a[1] | a[2] | a[3] | a[4] | a[5]); }
static inline bool is_multicast_ether_addr(const u8* a) { return a[0] & 0x01; }
static inline bool is_unicast_ether_addr(const u8* a) { return !is_multicast_ether_addr(a); }
static inline bool is_local_ether_addr(const u8* a) { return a[0] & 0x02; }
static inline bool is_broadcast_ether_addr(const u8* a) { return (a[0] & a[1] & a[2] & a[3] & a[4] & a[5]) == 0xff; }
static inline bool is_valid_ether_addr(const u8* a) { return !is_multicast_ether_addr(a) && !is_zero_ether_addr(a); }
static inline bool ether_addr_equal(const u8* a, const u8* b) { return !memcmp(a, b, ETH_ALEN); }
#define ether_addr_equal_unaligned ether_addr_equal
static inline void ether_addr_copy(u8* dst, const u8* src) { memcpy(dst, src, ETH_ALEN); }
static inline void eth_zero_addr(u8* a) { memset(a, 0, ETH_ALEN); }
static inline void eth_broadcast_addr(u8* a) { memset(a, 0xff, ETH_ALEN); }
static inline bool ether_addr_equal_masked(const u8* a, const u8* b, const u8* m) {
    for (int i = 0; i < ETH_ALEN; i++) if ((a[i] ^ b[i]) & m[i]) return false;
    return true;
}
static inline u64 ether_addr_to_u64(const u8* a) {
    u64 u = 0;
    for (int i = 0; i < ETH_ALEN; i++) u = u << 8 | a[i];
    return u;
}
void eth_random_addr(u8* addr);
static inline void eth_hw_addr_random(struct net_device* dev) {
    u8 a[ETH_ALEN];
    eth_random_addr(a);
    dev_addr_set(dev, a);
}
static inline void eth_hw_addr_set(struct net_device* dev, const u8* addr) { dev_addr_set(dev, addr); }
int eth_validate_addr(struct net_device* dev);
int eth_mac_addr(struct net_device* dev, void* p);
static inline int eth_skb_pad(struct sk_buff* skb) { return skb_put_padto(skb, ETH_ZLEN); }
static inline struct ethhdr* eth_hdr(const struct sk_buff* skb) { return (struct ethhdr*)skb_mac_header(skb); }

#endif

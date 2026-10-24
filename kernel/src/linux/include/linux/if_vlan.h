/* BrysOS: VLAN 802.1Q (BrysOS no usa VLAN; sólo lo que nombran los drivers) */
#ifndef _BRYS_LINUX_IF_VLAN_H
#define _BRYS_LINUX_IF_VLAN_H
#include <linux/netdevice.h>
#define VLAN_HLEN 4
#define VLAN_ETH_HLEN 18
#define VLAN_N_VID 4096
#define VLAN_VID_MASK 0x0fff
#define VLAN_PRIO_MASK 0xe000
#define VLAN_PRIO_SHIFT 13
#define VLAN_TAG_PRESENT 0x1000
static inline bool skb_vlan_tag_present(const struct sk_buff* skb) { return skb->vlan_proto != 0; }
static inline u16 skb_vlan_tag_get(const struct sk_buff* skb) { return skb->vlan_tci; }
static inline void __vlan_hwaccel_put_tag(struct sk_buff* skb, __be16 proto, u16 tci) { skb->vlan_proto = proto; skb->vlan_tci = tci; }
static inline __be16 vlan_get_protocol(const struct sk_buff* skb) { return skb->protocol; }
#endif

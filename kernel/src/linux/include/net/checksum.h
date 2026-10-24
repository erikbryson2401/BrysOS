/* BrysOS: sumas de verificación de Internet (brys/netdev.c) */
#ifndef _BRYS_NET_CHECKSUM_H
#define _BRYS_NET_CHECKSUM_H
#include <linux/types.h>
#include <linux/ip.h>
__sum16 csum_fold(__wsum csum);
__wsum csum_partial(const void* buf, int len, __wsum sum);
__sum16 ip_fast_csum(const void* iph, unsigned int ihl);
__wsum csum_tcpudp_nofold(__be32 saddr, __be32 daddr, u32 len, u8 proto, __wsum sum);
static inline __sum16 csum_tcpudp_magic(__be32 saddr, __be32 daddr, u32 len, u8 proto, __wsum sum) {
    return csum_fold(csum_tcpudp_nofold(saddr, daddr, len, proto, sum));
}
__sum16 csum_ipv6_magic(const struct in6_addr* saddr, const struct in6_addr* daddr, u32 len, u8 proto, __wsum sum);
static inline __wsum csum_unfold(__sum16 n) { return (__wsum)n; }
#define CSUM_MANGLED_0 ((__sum16)0xffff)
#endif

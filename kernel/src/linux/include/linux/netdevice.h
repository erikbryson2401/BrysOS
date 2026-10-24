/* BrysOS: dispositivos de red de Linux (struct net_device), NAPI y las colas de envío.
 * Lo implementa brys/netdev.c. Los paquetes recibidos (netif_receive_skb, napi_gro_receive)
 * van a la pila TCP/IP de BrysOS por brys_netdev_set_rx(). */
#ifndef _BRYS_LINUX_NETDEVICE_H
#define _BRYS_LINUX_NETDEVICE_H

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/device.h>
#include <linux/skbuff.h>
#include <linux/if_ether.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/atomic.h>
#include <linux/mutex.h>

#define IFNAMSIZ 16
#define MAX_ADDR_LEN 32

typedef u64 netdev_features_t;
#define NETIF_F_SG BIT_ULL(0)
#define NETIF_F_IP_CSUM BIT_ULL(1)
#define NETIF_F_HW_CSUM BIT_ULL(3)
#define NETIF_F_IPV6_CSUM BIT_ULL(4)
#define NETIF_F_HIGHDMA BIT_ULL(5)
#define NETIF_F_FRAGLIST BIT_ULL(6)
#define NETIF_F_HW_VLAN_CTAG_TX BIT_ULL(7)
#define NETIF_F_HW_VLAN_CTAG_RX BIT_ULL(8)
#define NETIF_F_HW_VLAN_CTAG_FILTER BIT_ULL(9)
#define NETIF_F_LLTX BIT_ULL(12)
#define NETIF_F_GRO BIT_ULL(14)
#define NETIF_F_TSO BIT_ULL(16)
#define NETIF_F_TSO6 BIT_ULL(19)
#define NETIF_F_RXCSUM BIT_ULL(29)
#define NETIF_F_RXFCS BIT_ULL(32)
#define NETIF_F_RXALL BIT_ULL(33)
#define NETIF_F_CSUM_MASK (NETIF_F_IP_CSUM | NETIF_F_HW_CSUM | NETIF_F_IPV6_CSUM)

#define IFF_UP 0x1
#define IFF_BROADCAST 0x2
#define IFF_DEBUG 0x4
#define IFF_LOOPBACK 0x8
#define IFF_RUNNING 0x40
#define IFF_NOARP 0x80
#define IFF_PROMISC 0x100
#define IFF_ALLMULTI 0x200
#define IFF_MULTICAST 0x1000
#define IFF_DOWN 0x0
/* priv_flags */
#define IFF_UNICAST_FLT BIT(13)
#define IFF_SUPP_NOFCS BIT(19)
#define IFF_LIVE_ADDR_CHANGE BIT(15)
#define IFF_DONT_BRIDGE BIT(7)
#define IFF_TX_SKB_SHARING BIT(10)
#define IFF_NO_QUEUE BIT(21)

#define ARPHRD_ETHER 1
#define ARPHRD_IEEE80211 801
#define ARPHRD_IEEE80211_RADIOTAP 803

typedef enum netdev_tx { NETDEV_TX_OK = 0, NETDEV_TX_BUSY = 0x10 } netdev_tx_t;

struct net_device_stats {
    unsigned long rx_packets, tx_packets, rx_bytes, tx_bytes;
    unsigned long rx_errors, tx_errors, rx_dropped, tx_dropped;
    unsigned long multicast, collisions;
    unsigned long rx_length_errors, rx_over_errors, rx_crc_errors, rx_frame_errors, rx_fifo_errors, rx_missed_errors;
    unsigned long tx_aborted_errors, tx_carrier_errors, tx_fifo_errors, tx_heartbeat_errors, tx_window_errors;
    unsigned long rx_compressed, tx_compressed;
};
struct rtnl_link_stats64 {
    u64 rx_packets, tx_packets, rx_bytes, tx_bytes, rx_errors, tx_errors, rx_dropped, tx_dropped;
};

struct net_device;
struct ifreq;
struct sockaddr {
    unsigned short sa_family;
    char sa_data[14];
};
struct ethtool_ops;

struct net_device_ops {
    int (*ndo_init)(struct net_device* dev);
    void (*ndo_uninit)(struct net_device* dev);
    int (*ndo_open)(struct net_device* dev);
    int (*ndo_stop)(struct net_device* dev);
    netdev_tx_t (*ndo_start_xmit)(struct sk_buff* skb, struct net_device* dev);
    u16 (*ndo_select_queue)(struct net_device* dev, struct sk_buff* skb, struct net_device* sb_dev);
    void (*ndo_set_rx_mode)(struct net_device* dev);
    int (*ndo_set_mac_address)(struct net_device* dev, void* addr);
    int (*ndo_validate_addr)(struct net_device* dev);
    int (*ndo_eth_ioctl)(struct net_device* dev, struct ifreq* ifr, int cmd);
    int (*ndo_do_ioctl)(struct net_device* dev, struct ifreq* ifr, int cmd);
    int (*ndo_change_mtu)(struct net_device* dev, int new_mtu);
    void (*ndo_tx_timeout)(struct net_device* dev, unsigned int txqueue);
    struct net_device_stats* (*ndo_get_stats)(struct net_device* dev);
    void (*ndo_get_stats64)(struct net_device* dev, struct rtnl_link_stats64* s);
    int (*ndo_vlan_rx_add_vid)(struct net_device* dev, __be16 proto, u16 vid);
    int (*ndo_vlan_rx_kill_vid)(struct net_device* dev, __be16 proto, u16 vid);
    void (*ndo_poll_controller)(struct net_device* dev);
    netdev_features_t (*ndo_fix_features)(struct net_device* dev, netdev_features_t features);
    int (*ndo_set_features)(struct net_device* dev, netdev_features_t features);
    netdev_features_t (*ndo_features_check)(struct sk_buff* skb, struct net_device* dev, netdev_features_t features);
};

/* Direcciones de unidifusión y multidifusión (filtros de la tarjeta) */
struct netdev_hw_addr {
    struct list_head list;
    unsigned char addr[MAX_ADDR_LEN];
};
struct netdev_hw_addr_list {
    struct list_head list;
    int count;
};
#define netdev_hw_addr_list_count(l) ((l)->count)
#define netdev_uc_count(dev) netdev_hw_addr_list_count(&(dev)->uc)
#define netdev_mc_count(dev) netdev_hw_addr_list_count(&(dev)->mc)
#define netdev_mc_empty(dev) (netdev_mc_count(dev) == 0)
#define netdev_for_each_uc_addr(ha, dev) list_for_each_entry(ha, &(dev)->uc.list, list)
#define netdev_for_each_mc_addr(ha, dev) list_for_each_entry(ha, &(dev)->mc.list, list)

struct netdev_queue {
    struct net_device* dev;
    unsigned long state;
    unsigned long trans_start;
};
#define __QUEUE_STATE_DRV_XOFF 0

enum netdev_state_t { __LINK_STATE_START, __LINK_STATE_PRESENT, __LINK_STATE_NOCARRIER };

struct napi_struct {
    struct list_head poll_list;
    unsigned long state;
    int weight;
    int (*poll)(struct napi_struct* napi, int budget);
    struct net_device* dev;
    struct list_head dev_list;
    struct sk_buff* skb;
};
enum { NAPI_STATE_SCHED, NAPI_STATE_DISABLE };
#define NAPI_POLL_WEIGHT 64

struct net_device {
    char name[IFNAMSIZ];
    struct device dev;
    unsigned long state;
    unsigned int flags;
    unsigned int priv_flags;
    netdev_features_t features, hw_features, wanted_features, vlan_features, hw_enc_features;
    const struct net_device_ops* netdev_ops;
    const struct ethtool_ops* ethtool_ops;
    struct net_device_stats stats;
    unsigned int mtu, min_mtu, max_mtu;
    unsigned short type;
    unsigned short hard_header_len;
    unsigned short needed_headroom, needed_tailroom;
    unsigned char addr_len;
    unsigned char perm_addr[MAX_ADDR_LEN];
    const unsigned char* dev_addr;
    unsigned char brys_dev_addr[MAX_ADDR_LEN];
    unsigned char broadcast[MAX_ADDR_LEN];
    struct netdev_hw_addr_list uc, mc;
    int watchdog_timeo;
    int irq;
    unsigned long mem_start, mem_end, base_addr;
    unsigned int tx_queue_len;
    unsigned int num_tx_queues, real_num_tx_queues;
    struct netdev_queue* _tx;
    struct list_head napi_list;
    void* ml_priv;
    struct wireless_dev* ieee80211_ptr;
    const void* wireless_handlers;
    void (*priv_destructor)(struct net_device* dev);
    bool needs_free_netdev;
    int ifindex;
    unsigned long trans_start;
    struct list_head brys_node;   /* BrysOS: lista de dispositivos de red */
    void* brys_stack;             /* BrysOS: interfaz de la pila TCP/IP (lwIP) */
    void* brys_priv;
};

static inline void* netdev_priv(const struct net_device* dev) { return dev->brys_priv; }
#define SET_NETDEV_DEV(net, pdev) ((net)->dev.parent = (pdev))
#define SET_NETDEV_DEVTYPE(net, t) do { } while (0)
static inline void dev_addr_set(struct net_device* dev, const u8* addr) { memcpy(dev->brys_dev_addr, addr, dev->addr_len); }
static inline void dev_addr_mod(struct net_device* dev, unsigned int off, const void* addr, size_t len) {
    memcpy(dev->brys_dev_addr + off, addr, len);
}
static inline void dev_hold(struct net_device* dev) { (void)dev; }
static inline void dev_put(struct net_device* dev) { (void)dev; }
#define netdev_hold(dev, t, gfp) dev_hold(dev)
#define netdev_put(dev, t) dev_put(dev)

struct net_device* alloc_netdev_mqs(int sizeof_priv, const char* name, unsigned char name_assign_type,
                                    void (*setup)(struct net_device*), unsigned int txqs, unsigned int rxqs);
#define alloc_netdev(sizeof_priv, name, assign, setup) alloc_netdev_mqs(sizeof_priv, name, assign, setup, 1, 1)
#define NET_NAME_UNKNOWN 0
#define NET_NAME_ENUM 1
void free_netdev(struct net_device* dev);
int register_netdev(struct net_device* dev);
int register_netdevice(struct net_device* dev);
void unregister_netdev(struct net_device* dev);
void unregister_netdevice(struct net_device* dev);
int dev_open(struct net_device* dev, void* extack);
int dev_close(struct net_device* dev);
void ether_setup(struct net_device* dev);

static inline bool netif_running(const struct net_device* dev) { return test_bit(__LINK_STATE_START, &dev->state); }
static inline bool netif_device_present(const struct net_device* dev) { return test_bit(__LINK_STATE_PRESENT, &dev->state); }
static inline void netif_device_detach(struct net_device* dev) { clear_bit(__LINK_STATE_PRESENT, &dev->state); }
static inline void netif_device_attach(struct net_device* dev) { set_bit(__LINK_STATE_PRESENT, &dev->state); }
static inline bool netif_carrier_ok(const struct net_device* dev) { return !test_bit(__LINK_STATE_NOCARRIER, &dev->state); }
void netif_carrier_on(struct net_device* dev);
void netif_carrier_off(struct net_device* dev);
static inline bool netif_dormant(const struct net_device* dev) { (void)dev; return false; }

static inline struct netdev_queue* netdev_get_tx_queue(const struct net_device* dev, unsigned int i) { return &dev->_tx[i]; }
static inline void netif_tx_start_queue(struct netdev_queue* q) { clear_bit(__QUEUE_STATE_DRV_XOFF, &q->state); }
static inline void netif_tx_stop_queue(struct netdev_queue* q) { set_bit(__QUEUE_STATE_DRV_XOFF, &q->state); }
static inline bool netif_tx_queue_stopped(const struct netdev_queue* q) { return test_bit(__QUEUE_STATE_DRV_XOFF, &q->state); }
void netif_tx_wake_queue(struct netdev_queue* q);
#define netif_xmit_stopped netif_tx_queue_stopped
static inline void netif_start_queue(struct net_device* dev) { netif_tx_start_queue(netdev_get_tx_queue(dev, 0)); }
static inline void netif_stop_queue(struct net_device* dev) { netif_tx_stop_queue(netdev_get_tx_queue(dev, 0)); }
static inline void netif_wake_queue(struct net_device* dev) { netif_tx_wake_queue(netdev_get_tx_queue(dev, 0)); }
static inline bool netif_queue_stopped(const struct net_device* dev) { return netif_tx_queue_stopped(netdev_get_tx_queue(dev, 0)); }
static inline void netif_tx_disable(struct net_device* dev) {
    for (unsigned int i = 0; i < dev->num_tx_queues; i++) netif_tx_stop_queue(netdev_get_tx_queue(dev, i));
}
#define netif_tx_stop_all_queues netif_tx_disable
static inline void netif_tx_start_all_queues(struct net_device* dev) {
    for (unsigned int i = 0; i < dev->num_tx_queues; i++) netif_tx_start_queue(netdev_get_tx_queue(dev, i));
}
static inline void netif_tx_wake_all_queues(struct net_device* dev) {
    for (unsigned int i = 0; i < dev->num_tx_queues; i++) netif_tx_wake_queue(netdev_get_tx_queue(dev, i));
}
static inline int netif_set_real_num_tx_queues(struct net_device* dev, unsigned int n) { dev->real_num_tx_queues = n; return 0; }
static inline int netif_set_real_num_rx_queues(struct net_device* dev, unsigned int n) { (void)dev; (void)n; return 0; }
#define smp_mb__after_netif_stop_queue() smp_mb()
static inline bool netdev_xmit_more(void) { return false; }
static inline void netdev_sent_queue(struct net_device* dev, unsigned int bytes) { (void)dev; (void)bytes; }
static inline void netdev_completed_queue(struct net_device* dev, unsigned int pkts, unsigned int bytes) {
    (void)dev; (void)pkts; (void)bytes;
}
static inline void netdev_reset_queue(struct net_device* dev) { (void)dev; }
static inline void netdev_tx_sent_queue(struct netdev_queue* q, unsigned int b) { (void)q; (void)b; }
static inline void netdev_tx_completed_queue(struct netdev_queue* q, unsigned int p, unsigned int b) { (void)q; (void)p; (void)b; }
static inline void netdev_tx_reset_queue(struct netdev_queue* q) { (void)q; }
static inline void txq_trans_cond_update(struct netdev_queue* q) { q->trans_start = jiffies; }
static inline void netif_trans_update(struct net_device* dev) { dev->trans_start = jiffies; }
static inline void netdev_update_features(struct net_device* dev) { (void)dev; }

/* NAPI: los sondeos los ejecuta el hilo "linux-trabajos" */
void netif_napi_add_weight(struct net_device* dev, struct napi_struct* napi, int (*poll)(struct napi_struct*, int), int weight);
#define netif_napi_add(dev, napi, poll) netif_napi_add_weight(dev, napi, poll, NAPI_POLL_WEIGHT)
#define netif_napi_add_tx netif_napi_add
void netif_napi_del(struct napi_struct* napi);
void napi_enable(struct napi_struct* napi);
void napi_disable(struct napi_struct* napi);
bool napi_schedule_prep(struct napi_struct* napi);
void __napi_schedule(struct napi_struct* napi);
#define __napi_schedule_irqoff __napi_schedule
static inline bool napi_schedule(struct napi_struct* napi) {
    if (napi_schedule_prep(napi)) { __napi_schedule(napi); return true; }
    return false;
}
#define napi_schedule_irqoff napi_schedule
bool napi_complete_done(struct napi_struct* napi, int work_done);
#define napi_complete(n) napi_complete_done(n, 0)
typedef enum gro_result { GRO_MERGED, GRO_MERGED_FREE, GRO_HELD, GRO_NORMAL, GRO_CONSUMED } gro_result_t;
gro_result_t napi_gro_receive(struct napi_struct* napi, struct sk_buff* skb);
struct sk_buff* napi_get_frags(struct napi_struct* napi);
gro_result_t napi_gro_frags(struct napi_struct* napi);
void napi_free_frags(struct napi_struct* napi);
int netif_receive_skb(struct sk_buff* skb);
int netif_rx(struct sk_buff* skb);
#define netif_rx_ni netif_rx
int dev_queue_xmit(struct sk_buff* skb);
__be16 eth_type_trans(struct sk_buff* skb, struct net_device* dev);

/* Mensajes */
#define netdev_err(dev, fmt, ...) printk(KERN_ERR "%s: " fmt, (dev)->name, ##__VA_ARGS__)
#define netdev_warn(dev, fmt, ...) printk(KERN_WARNING "%s: " fmt, (dev)->name, ##__VA_ARGS__)
#define netdev_info(dev, fmt, ...) printk(KERN_INFO "%s: " fmt, (dev)->name, ##__VA_ARGS__)
#define netdev_notice(dev, fmt, ...) printk(KERN_NOTICE "%s: " fmt, (dev)->name, ##__VA_ARGS__)
#define netdev_dbg(dev, fmt, ...) ({ if (0) printk(KERN_DEBUG "%s: " fmt, (dev)->name, ##__VA_ARGS__); })
#define netdev_err_once netdev_err
#define netdev_warn_once netdev_warn
#define netif_err(priv, type, dev, fmt, ...) netdev_err(dev, fmt, ##__VA_ARGS__)
#define netif_warn(priv, type, dev, fmt, ...) netdev_warn(dev, fmt, ##__VA_ARGS__)
#define netif_notice(priv, type, dev, fmt, ...) netdev_notice(dev, fmt, ##__VA_ARGS__)
#define netif_info(priv, type, dev, fmt, ...) netdev_info(dev, fmt, ##__VA_ARGS__)
#define netif_dbg(priv, type, dev, fmt, ...) netdev_dbg(dev, fmt, ##__VA_ARGS__)
#define NETIF_MSG_DRV 0x0001
#define NETIF_MSG_PROBE 0x0002
#define NETIF_MSG_LINK 0x0004
#define NETIF_MSG_TIMER 0x0008
#define NETIF_MSG_IFDOWN 0x0010
#define NETIF_MSG_IFUP 0x0020
#define NETIF_MSG_RX_ERR 0x0040
#define NETIF_MSG_TX_ERR 0x0080
#define NETIF_MSG_TX_QUEUED 0x0100
#define NETIF_MSG_INTR 0x0200
#define NETIF_MSG_TX_DONE 0x0400
#define NETIF_MSG_RX_STATUS 0x0800
#define NETIF_MSG_PKTDATA 0x1000
#define NETIF_MSG_HW 0x2000
#define NETIF_MSG_WOL 0x4000
static inline u32 netif_msg_init(int debug_value, int default_msg_enable_bits) {
    if (debug_value < 0 || debug_value >= 32) return (u32)default_msg_enable_bits;
    return debug_value ? (1U << debug_value) - 1 : 0;
}
#define netif_msg_drv(p) ((p)->msg_enable & NETIF_MSG_DRV)
#define netif_msg_probe(p) ((p)->msg_enable & NETIF_MSG_PROBE)
#define netif_msg_link(p) ((p)->msg_enable & NETIF_MSG_LINK)
#define netif_msg_timer(p) ((p)->msg_enable & NETIF_MSG_TIMER)
#define netif_msg_ifdown(p) ((p)->msg_enable & NETIF_MSG_IFDOWN)
#define netif_msg_ifup(p) ((p)->msg_enable & NETIF_MSG_IFUP)
#define netif_msg_rx_err(p) ((p)->msg_enable & NETIF_MSG_RX_ERR)
#define netif_msg_tx_err(p) ((p)->msg_enable & NETIF_MSG_TX_ERR)
#define netif_msg_tx_queued(p) ((p)->msg_enable & NETIF_MSG_TX_QUEUED)
#define netif_msg_intr(p) ((p)->msg_enable & NETIF_MSG_INTR)
#define netif_msg_tx_done(p) ((p)->msg_enable & NETIF_MSG_TX_DONE)
#define netif_msg_rx_status(p) ((p)->msg_enable & NETIF_MSG_RX_STATUS)
#define netif_msg_pktdata(p) ((p)->msg_enable & NETIF_MSG_PKTDATA)
#define netif_msg_hw(p) ((p)->msg_enable & NETIF_MSG_HW)
#define netif_msg_wol(p) ((p)->msg_enable & NETIF_MSG_WOL)

/* RTNL: un mutex global */
void rtnl_lock(void);
void rtnl_unlock(void);
int rtnl_trylock(void);
int rtnl_is_locked(void);
#define ASSERT_RTNL() do { } while (0)
#define rtnl_dereference(p) (p)

/* BrysOS: lista de dispositivos y enlace con la pila TCP/IP */
typedef void (*brys_netdev_rx_fn)(struct net_device* dev, struct sk_buff* skb);
void brys_netdev_set_rx(brys_netdev_rx_fn fn);
typedef void (*brys_netdev_link_fn)(struct net_device* dev, int up);
void brys_netdev_set_link(brys_netdev_link_fn fn);
int brys_netdev_count(void);
struct net_device* brys_netdev_get(int i);
int brys_netdev_xmit(struct net_device* dev, const void* data, unsigned int len);

#include <linux/etherdevice.h>

#endif

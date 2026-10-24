/* Capa de compatibilidad con Linux: red. Páginas (struct page), paquetes (sk_buff),
 * dispositivos de red (net_device) y sumas de verificación. Los paquetes que reciben los
 * drivers van a la pila TCP/IP de BrysOS (lwIP) por la función de brys_netdev_set_rx(). */
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/mii.h>
#include <net/checksum.h>
#include <brys/glue.h>
#include <brys/linux.h>

/* ------------------------------------------------------------------------------------
 * Páginas. Cada página tiene un descriptor; una tabla (dirección -> descriptor) permite
 * encontrarlo desde cualquier puntero de dentro (virt_to_page, skb_free_frag). Las páginas
 * sueltas que se liberan se guardan para reutilizarlas: un paquete no debería costar una
 * búsqueda en el mapa de memoria física.
 * ------------------------------------------------------------------------------------ */
#define PAGE_TABLE 8192
static struct page* page_table[PAGE_TABLE];
static struct page* pool[256];
static int pool_count;

static unsigned int page_slot(unsigned long addr) { return (unsigned int)((addr >> 12) * 2654435761u) % PAGE_TABLE; }

static void table_add(struct page* p) {
    unsigned int pages = 1u << p->brys_order;
    for (unsigned int i = 0; i < pages; i++) {
        unsigned long a = (unsigned long)p->brys_addr + i * PAGE_SIZE;
        for (unsigned int s = page_slot(a), n = 0; n < PAGE_TABLE; s = (s + 1) % PAGE_TABLE, n++)
            if (!page_table[s] || page_table[s] == (struct page*)1) { page_table[s] = p; break; }
    }
}

static void table_del(struct page* p) {
    unsigned int pages = 1u << p->brys_order;
    for (unsigned int i = 0; i < pages; i++) {
        unsigned long a = (unsigned long)p->brys_addr + i * PAGE_SIZE;
        for (unsigned int s = page_slot(a), n = 0; n < PAGE_TABLE && page_table[s]; s = (s + 1) % PAGE_TABLE, n++)
            if (page_table[s] == p) { page_table[s] = (struct page*)1; break; }   /* 1 = hueco borrado */
    }
}

struct page* virt_to_page(const void* addr) {
    unsigned long base = (unsigned long)addr & PAGE_MASK;
    /* una página de un bloque de varias: se busca hacia atrás hasta el comienzo */
    for (unsigned int s = page_slot(base), n = 0; n < PAGE_TABLE && page_table[s]; s = (s + 1) % PAGE_TABLE, n++) {
        struct page* p = page_table[s];
        if (p == (struct page*)1) continue;
        unsigned long start = (unsigned long)p->brys_addr;
        if (base >= start && base < start + (PAGE_SIZE << p->brys_order)) return p;
    }
    return NULL;
}

struct page* alloc_pages(gfp_t gfp, unsigned int order) {
    unsigned long f = brys_irq_save();
    if (order == 0 && pool_count) {
        struct page* p = pool[--pool_count];
        brys_irq_restore(f);
        p->brys_refs = 1;
        if (gfp & __GFP_ZERO) memset(p->brys_addr, 0, PAGE_SIZE);
        return p;
    }
    brys_irq_restore(f);
    struct page* p = brys_kmalloc(sizeof *p);
    if (!p) return NULL;
    uint64_t phys;
    p->brys_addr = brys_dma_alloc(PAGE_SIZE << order, &phys);
    if (!p->brys_addr) { brys_kfree(p); return NULL; }
    p->brys_order = order;
    p->brys_refs = 1;
    f = brys_irq_save();
    table_add(p);
    brys_irq_restore(f);
    return p;
}

void __free_pages(struct page* p, unsigned int order) {
    (void)order;
    if (!p) return;
    unsigned long f = brys_irq_save();
    if (p->brys_order == 0 && pool_count < (int)ARRAY_SIZE(pool)) {
        pool[pool_count++] = p;
        brys_irq_restore(f);
        return;
    }
    table_del(p);
    brys_irq_restore(f);
    brys_dma_free(p->brys_addr, PAGE_SIZE << p->brys_order);
    brys_kfree(p);
}

void put_page(struct page* p) {
    if (p && --p->brys_refs <= 0) __free_pages(p, p->brys_order);
}

phys_addr_t page_to_phys(const struct page* p) { return brys_virt_to_phys(p->brys_addr); }

dma_addr_t dma_map_page(struct device* dev, struct page* page, size_t off, size_t size, int dir) {
    (void)dev; (void)size; (void)dir;
    return page_to_phys(page) + off;
}

static unsigned int order_for(unsigned int size) {
    unsigned int order = 0;
    while ((PAGE_SIZE << order) < size) order++;
    return order;
}

void* netdev_alloc_frag(unsigned int fragsz) {
    struct page* p = alloc_pages(GFP_ATOMIC, order_for(fragsz));
    return p ? p->brys_addr : NULL;
}

void skb_free_frag(void* addr) {
    struct page* p = virt_to_page(addr);
    if (p) put_page(p);
    else printk(KERN_ERR "skb_free_frag: %p no es de una página conocida\n", addr);
}

/* ------------------------------------------------------------------------------------
 * Paquetes (sk_buff)
 * ------------------------------------------------------------------------------------ */
static struct sk_buff* skb_around(void* data, unsigned int bufsize) {
    struct sk_buff* skb = kzalloc(sizeof *skb, GFP_ATOMIC);
    if (!skb) return NULL;
    unsigned int shinfo = (unsigned int)SKB_DATA_ALIGN(sizeof(struct skb_shared_info));
    skb->head = skb->data = data;
    skb->tail = 0;
    skb->end = bufsize - shinfo;
    skb->brys_bufsize = bufsize;
    skb->truesize = SKB_TRUESIZE(skb->end);
    refcount_set(&skb->users, 1);
    skb->mac_header = skb->network_header = skb->transport_header = 0xffff;
    memset(skb_shinfo(skb), 0, sizeof(struct skb_shared_info));
    return skb;
}

struct sk_buff* alloc_skb(unsigned int size, gfp_t gfp) {
    (void)gfp;
    unsigned int bufsize = (unsigned int)(SKB_DATA_ALIGN(size) + SKB_DATA_ALIGN(sizeof(struct skb_shared_info)));
    void* data = netdev_alloc_frag(bufsize);
    if (!data) return NULL;
    struct page* p = virt_to_page(data);
    struct sk_buff* skb = skb_around(data, PAGE_SIZE << p->brys_order);
    if (!skb) skb_free_frag(data);
    return skb;
}

struct sk_buff* __alloc_skb(unsigned int size, gfp_t gfp, int flags, int node) {
    (void)flags; (void)node;
    return alloc_skb(size, gfp);
}

struct sk_buff* build_skb(void* data, unsigned int frag_size) {
    struct sk_buff* skb = skb_around(data, frag_size ? frag_size : (unsigned int)PAGE_SIZE);
    if (skb) skb->head_frag = 1;
    return skb;
}

struct sk_buff* __netdev_alloc_skb(struct net_device* dev, unsigned int len, gfp_t gfp) {
    struct sk_buff* skb = alloc_skb(len + NET_SKB_PAD, gfp);
    if (!skb) return NULL;
    skb_reserve(skb, NET_SKB_PAD);
    skb->dev = dev;
    return skb;
}

struct sk_buff* napi_alloc_skb(struct napi_struct* napi, unsigned int len) {
    return __netdev_alloc_skb(napi ? napi->dev : NULL, len, GFP_ATOMIC);
}

static void free_skb(struct sk_buff* skb) {
    struct skb_shared_info* sh = skb_shinfo(skb);
    for (int i = 0; i < sh->nr_frags; i++) put_page(sh->frags[i].bv_page);
    if (sh->frag_list) kfree_skb(sh->frag_list);
    if (skb->destructor) skb->destructor(skb);
    skb_free_frag(skb->head);
    kfree(skb);
}

void kfree_skb(struct sk_buff* skb) {
    if (!skb) return;
    if (refcount_dec_and_test(&skb->users)) free_skb(skb);
}

void consume_skb(struct sk_buff* skb) { kfree_skb(skb); }

void* skb_put(struct sk_buff* skb, unsigned int len) {
    void* tmp = skb_tail_pointer(skb);
    skb->tail += len;
    skb->len += len;
    if (skb->tail > skb->end) brys_linux_bug(__FILE__, __LINE__);
    return tmp;
}

void* skb_push(struct sk_buff* skb, unsigned int len) {
    skb->data -= len;
    skb->len += len;
    if (skb->data < skb->head) brys_linux_bug(__FILE__, __LINE__);
    return skb->data;
}

void* skb_pull(struct sk_buff* skb, unsigned int len) {
    if (len > skb->len) return NULL;
    skb->len -= len;
    return skb->data += len;
}

void skb_trim(struct sk_buff* skb, unsigned int len) {
    if (skb->len > len && !skb->data_len) {
        skb->len = len;
        skb_set_tail_pointer(skb, (int)len);
    }
}

void* __pskb_pull_tail(struct sk_buff* skb, int delta) {
    (void)delta;   /* los paquetes de BrysOS son lineales */
    return skb_tail_pointer(skb);
}

int skb_copy_bits(const struct sk_buff* skb, int offset, void* to, int len) {
    if (offset < 0 || offset + len > (int)skb->len) return -EFAULT;
    int head = (int)skb_headlen(skb);
    u8* out = to;
    int copy = offset < head ? min(head - offset, len) : 0;
    if (copy) { memcpy(out, skb->data + offset, (size_t)copy); out += copy; len -= copy; offset += copy; }
    int pos = head;
    for (int i = 0; len > 0 && i < skb_shinfo(skb)->nr_frags; i++) {
        const skb_frag_t* f = &skb_shinfo(skb)->frags[i];
        int fs = (int)skb_frag_size(f);
        if (offset < pos + fs) {
            int o = offset - pos;
            int n = min(fs - o, len);
            memcpy(out, (u8*)skb_frag_address(f) + o, (size_t)n);
            out += n; len -= n; offset += n;
        }
        pos += fs;
    }
    return len ? -EFAULT : 0;
}

void skb_add_rx_frag(struct sk_buff* skb, int i, struct page* page, int off, int size, unsigned int truesize) {
    skb_fill_page_desc(skb, i, page, off, size);
    skb->len += (unsigned int)size;
    skb->data_len += (unsigned int)size;
    skb->truesize += truesize;
}

/* Copia lineal (los fragmentos pasan a la parte principal) */
struct sk_buff* skb_copy_expand(const struct sk_buff* skb, int newheadroom, int newtailroom, gfp_t gfp) {
    struct sk_buff* n = alloc_skb((unsigned int)(newheadroom + (int)skb->len + newtailroom), gfp);
    if (!n) return NULL;
    skb_reserve(n, newheadroom);
    skb_put(n, skb->len);
    skb_copy_bits(skb, 0, n->data, (int)skb->len);
    memcpy(n->cb, skb->cb, sizeof n->cb);
    n->dev = skb->dev;
    n->protocol = skb->protocol;
    n->priority = skb->priority;
    n->pkt_type = skb->pkt_type;
    n->ip_summed = skb->ip_summed;
    n->queue_mapping = skb->queue_mapping;
    if (skb->mac_header != 0xffff) n->mac_header = (u16)(skb->mac_header - skb_headroom(skb) + newheadroom);
    if (skb->network_header != 0xffff) n->network_header = (u16)(skb->network_header - skb_headroom(skb) + newheadroom);
    if (skb->transport_header != 0xffff) n->transport_header = (u16)(skb->transport_header - skb_headroom(skb) + newheadroom);
    return n;
}

struct sk_buff* skb_copy(const struct sk_buff* skb, gfp_t gfp) {
    return skb_copy_expand(skb, (int)skb_headroom(skb), skb_tailroom(skb) > 0 ? skb_tailroom(skb) : 0, gfp);
}

/* BrysOS no comparte búferes entre clones: clonar es copiar */
struct sk_buff* skb_clone(struct sk_buff* skb, gfp_t gfp) { return skb_copy(skb, gfp); }

int pskb_expand_head(struct sk_buff* skb, int nhead, int ntail, gfp_t gfp) {
    struct sk_buff* n = skb_copy_expand(skb, (int)skb_headroom(skb) + nhead, skb_tailroom(skb) + ntail, gfp);
    if (!n) return -ENOMEM;
    /* el paquete conserva su struct (los demás pueden tener punteros a él): se cambia el búfer */
    struct skb_shared_info* sh = skb_shinfo(skb);
    for (int i = 0; i < sh->nr_frags; i++) put_page(sh->frags[i].bv_page);
    skb_free_frag(skb->head);
    skb->head = n->head;
    skb->data = n->data;
    skb->tail = n->tail;
    skb->end = n->end;
    skb->brys_bufsize = n->brys_bufsize;
    skb->data_len = 0;
    skb->mac_header = n->mac_header;
    skb->network_header = n->network_header;
    skb->transport_header = n->transport_header;
    kfree(n);   /* sólo la struct: el búfer ya es de skb */
    return 0;
}

void skb_queue_tail(struct sk_buff_head* l, struct sk_buff* n) {
    unsigned long f = brys_irq_save();
    __skb_queue_tail(l, n);
    brys_irq_restore(f);
}
void skb_queue_head(struct sk_buff_head* l, struct sk_buff* n) {
    unsigned long f = brys_irq_save();
    __skb_queue_head(l, n);
    brys_irq_restore(f);
}
struct sk_buff* skb_dequeue(struct sk_buff_head* l) {
    unsigned long f = brys_irq_save();
    struct sk_buff* s = __skb_dequeue(l);
    brys_irq_restore(f);
    return s;
}
struct sk_buff* skb_dequeue_tail(struct sk_buff_head* l) {
    unsigned long f = brys_irq_save();
    struct sk_buff* s = __skb_dequeue_tail(l);
    brys_irq_restore(f);
    return s;
}
void skb_unlink(struct sk_buff* skb, struct sk_buff_head* l) {
    unsigned long f = brys_irq_save();
    __skb_unlink(skb, l);
    brys_irq_restore(f);
}
void skb_queue_purge(struct sk_buff_head* l) {
    struct sk_buff* s;
    while ((s = skb_dequeue(l))) kfree_skb(s);
}

/* ------------------------------------------------------------------------------------
 * Dispositivos de red
 * ------------------------------------------------------------------------------------ */
static LIST_HEAD(netdevs);
static int netdev_count;
static brys_netdev_rx_fn rx_fn;
static brys_netdev_link_fn link_fn;
static DEFINE_MUTEX(rtnl);

void rtnl_lock(void) { mutex_lock(&rtnl); }
void rtnl_unlock(void) { mutex_unlock(&rtnl); }
int rtnl_trylock(void) { return mutex_trylock(&rtnl); }
int rtnl_is_locked(void) { return mutex_is_locked(&rtnl); }

void ether_setup(struct net_device* dev) {
    dev->type = ARPHRD_ETHER;
    dev->hard_header_len = ETH_HLEN;
    dev->mtu = ETH_DATA_LEN;
    dev->min_mtu = ETH_MIN_MTU;
    dev->max_mtu = ETH_DATA_LEN;
    dev->addr_len = ETH_ALEN;
    dev->tx_queue_len = 1000;
    dev->flags = IFF_BROADCAST | IFF_MULTICAST;
    memset(dev->broadcast, 0xff, ETH_ALEN);
}

struct net_device* alloc_netdev_mqs(int sizeof_priv, const char* name, unsigned char name_assign_type,
                                    void (*setup)(struct net_device*), unsigned int txqs, unsigned int rxqs) {
    (void)name_assign_type; (void)rxqs;
    if (!txqs) txqs = 1;
    size_t base = ALIGN(sizeof(struct net_device), 32);
    struct net_device* dev = kzalloc(base + (size_t)sizeof_priv, GFP_KERNEL);
    if (!dev) return NULL;
    dev->_tx = kcalloc(txqs, sizeof(struct netdev_queue), GFP_KERNEL);
    if (!dev->_tx) { kfree(dev); return NULL; }
    for (unsigned int i = 0; i < txqs; i++) dev->_tx[i].dev = dev;
    dev->num_tx_queues = dev->real_num_tx_queues = txqs;
    dev->brys_priv = sizeof_priv ? (u8*)dev + base : NULL;
    dev->dev_addr = dev->brys_dev_addr;
    strscpy(dev->name, name, sizeof dev->name);
    INIT_LIST_HEAD(&dev->napi_list);
    INIT_LIST_HEAD(&dev->uc.list);
    INIT_LIST_HEAD(&dev->mc.list);
    INIT_LIST_HEAD(&dev->brys_node);
    device_initialize(&dev->dev);
    set_bit(__LINK_STATE_PRESENT, &dev->state);
    set_bit(__LINK_STATE_NOCARRIER, &dev->state);
    if (setup) setup(dev);
    return dev;
}

struct net_device* alloc_etherdev_mqs(int sizeof_priv, unsigned int txqs, unsigned int rxqs) {
    return alloc_netdev_mqs(sizeof_priv, "eth%d", NET_NAME_UNKNOWN, ether_setup, txqs, rxqs);
}

void free_netdev(struct net_device* dev) {
    if (!dev) return;
    kfree(dev->_tx);
    kfree(dev);
}

int register_netdevice(struct net_device* dev) {
    if (strchr(dev->name, '%')) {
        /* "eth%d" -> eth0, eth1... según los que ya hay con ese prefijo */
        int n = 0;
        struct net_device* d;
        list_for_each_entry(d, &netdevs, brys_node) if (!strncmp(d->name, dev->name, 3)) n++;
        char fmt[IFNAMSIZ];
        strscpy(fmt, dev->name, sizeof fmt);
        snprintf(dev->name, sizeof dev->name, fmt, n);
    }
    if (dev->netdev_ops && dev->netdev_ops->ndo_init) {
        int err = dev->netdev_ops->ndo_init(dev);
        if (err) return err;
    }
    dev->ifindex = ++netdev_count;
    list_add_tail(&dev->brys_node, &netdevs);
    printk(KERN_INFO "%s: dispositivo de red %pM\n", dev->name, dev->dev_addr);
    return 0;
}

int register_netdev(struct net_device* dev) {
    rtnl_lock();
    int err = register_netdevice(dev);
    rtnl_unlock();
    return err;
}

void unregister_netdevice(struct net_device* dev) {
    if (netif_running(dev)) dev_close(dev);
    list_del_init(&dev->brys_node);
}

void unregister_netdev(struct net_device* dev) {
    rtnl_lock();
    unregister_netdevice(dev);
    rtnl_unlock();
}

int dev_open(struct net_device* dev, void* extack) {
    (void)extack;
    if (netif_running(dev)) return 0;
    set_bit(__LINK_STATE_START, &dev->state);
    int err = dev->netdev_ops->ndo_open ? dev->netdev_ops->ndo_open(dev) : 0;
    if (err) {
        clear_bit(__LINK_STATE_START, &dev->state);
        printk(KERN_ERR "%s: no se ha podido abrir (%d)\n", dev->name, err);
        return err;
    }
    dev->flags |= IFF_UP;
    if (dev->netdev_ops->ndo_set_rx_mode) dev->netdev_ops->ndo_set_rx_mode(dev);
    return 0;
}

int dev_close(struct net_device* dev) {
    if (!netif_running(dev)) return 0;
    if (dev->netdev_ops->ndo_stop) dev->netdev_ops->ndo_stop(dev);
    clear_bit(__LINK_STATE_START, &dev->state);
    dev->flags &= ~IFF_UP;
    return 0;
}

void netif_carrier_on(struct net_device* dev) {
    if (test_and_clear_bit(__LINK_STATE_NOCARRIER, &dev->state)) {
        printk(KERN_INFO "%s: enlace activo\n", dev->name);
        if (link_fn) link_fn(dev, 1);
    }
}

void netif_carrier_off(struct net_device* dev) {
    if (!test_and_set_bit(__LINK_STATE_NOCARRIER, &dev->state)) {
        if (dev->ifindex) printk(KERN_INFO "%s: sin enlace\n", dev->name);
        if (link_fn) link_fn(dev, 0);
    }
}

void netif_tx_wake_queue(struct netdev_queue* q) { clear_bit(__QUEUE_STATE_DRV_XOFF, &q->state); }

__be16 eth_type_trans(struct sk_buff* skb, struct net_device* dev) {
    skb->dev = dev;
    skb_reset_mac_header(skb);
    const struct ethhdr* eth = (const struct ethhdr*)skb->data;
    skb_pull(skb, ETH_HLEN);
    skb_reset_network_header(skb);
    if (is_multicast_ether_addr(eth->h_dest))
        skb->pkt_type = is_broadcast_ether_addr(eth->h_dest) ? PACKET_BROADCAST : PACKET_MULTICAST;
    else if (!ether_addr_equal(eth->h_dest, dev->dev_addr))
        skb->pkt_type = PACKET_OTHERHOST;
    return eth->h_proto;
}

int netif_receive_skb(struct sk_buff* skb) {
    struct net_device* dev = skb->dev;
    if (dev) { dev->stats.rx_packets++; dev->stats.rx_bytes += skb->len; }
    if (rx_fn && dev) rx_fn(dev, skb);
    else kfree_skb(skb);
    return 0;
}

int netif_rx(struct sk_buff* skb) { return netif_receive_skb(skb); }

gro_result_t napi_gro_receive(struct napi_struct* napi, struct sk_buff* skb) {
    (void)napi;
    netif_receive_skb(skb);
    return GRO_NORMAL;
}

struct sk_buff* napi_get_frags(struct napi_struct* napi) {
    if (!napi->skb) napi->skb = napi_alloc_skb(napi, 128);
    return napi->skb;
}

gro_result_t napi_gro_frags(struct napi_struct* napi) {
    struct sk_buff* skb = napi->skb;
    napi->skb = NULL;
    if (!skb) return GRO_NORMAL;
    /* el paquete viene en fragmentos de página: se copia a uno lineal para la pila */
    struct sk_buff* lin = skb_copy_expand(skb, NET_SKB_PAD, 0, GFP_ATOMIC);
    kfree_skb(skb);
    if (!lin) return GRO_NORMAL;
    lin->protocol = eth_type_trans(lin, napi->dev);
    netif_receive_skb(lin);
    return GRO_NORMAL;
}

void napi_free_frags(struct napi_struct* napi) {
    kfree_skb(napi->skb);
    napi->skb = NULL;
}

int dev_queue_xmit(struct sk_buff* skb) {
    struct net_device* dev = skb->dev;
    if (!dev || !netif_running(dev) || !netif_carrier_ok(dev) || netif_queue_stopped(dev)) {
        if (dev) dev->stats.tx_dropped++;
        kfree_skb(skb);
        return -ENETDOWN;
    }
    netdev_tx_t r = dev->netdev_ops->ndo_start_xmit(skb, dev);
    if (r != NETDEV_TX_OK) {
        dev->stats.tx_dropped++;
        kfree_skb(skb);
        return -EBUSY;
    }
    return 0;
}

void eth_random_addr(u8* addr) {
    u64 t = brys_ns();
    for (int i = 0; i < ETH_ALEN; i++) { t = t * 6364136223846793005ULL + 1442695040888963407ULL; addr[i] = (u8)(t >> 56); }
    addr[0] &= 0xfe;   /* unidifusión */
    addr[0] |= 0x02;   /* administrada localmente */
}

int eth_validate_addr(struct net_device* dev) { return is_valid_ether_addr(dev->dev_addr) ? 0 : -EADDRNOTAVAIL; }

int eth_mac_addr(struct net_device* dev, void* p) {
    struct sockaddr* addr = p;
    if (!is_valid_ether_addr((const u8*)addr->sa_data)) return -EADDRNOTAVAIL;
    eth_hw_addr_set(dev, (const u8*)addr->sa_data);
    return 0;
}

struct mii_ioctl_data* if_mii(struct ifreq* rq) { (void)rq; return NULL; }   /* BrysOS no tiene ioctl de red */

/* BrysOS: la pila TCP/IP */
void brys_netdev_set_rx(brys_netdev_rx_fn fn) { rx_fn = fn; }
void brys_netdev_set_link(brys_netdev_link_fn fn) { link_fn = fn; }

int brys_netdev_count(void) { return netdev_count; }

struct net_device* brys_netdev_get(int i) {
    struct net_device* d;
    list_for_each_entry(d, &netdevs, brys_node) if (i-- == 0) return d;
    return NULL;
}

int brys_netdev_xmit(struct net_device* dev, const void* data, unsigned int len) {
    struct sk_buff* skb = __netdev_alloc_skb(dev, len, GFP_ATOMIC);
    if (!skb) { dev->stats.tx_dropped++; return -ENOMEM; }
    skb_put_data(skb, data, len);
    skb_reset_mac_header(skb);
    if (len >= ETH_HLEN) skb->protocol = ((const struct ethhdr*)data)->h_proto;
    skb_set_network_header(skb, ETH_HLEN);
    skb->ip_summed = CHECKSUM_NONE;
    return dev_queue_xmit(skb);
}

/* La misma interfaz sin tipos de Linux (brys/linux.h), para la pila TCP/IP */
static brys_net_rx_fn raw_rx;
static brys_net_link_fn raw_link;
static u8 linear_buf[16384];

static void raw_rx_adapter(struct net_device* dev, struct sk_buff* skb) {
    /* la trama completa empieza en la cabecera Ethernet (eth_type_trans la había saltado) */
    u8* start = skb->mac_header != 0xffff ? skb_mac_header(skb) : skb->data;
    unsigned int before = (unsigned int)(skb->data - start);
    unsigned int len = skb->len + before;
    if (!skb_is_nonlinear(skb)) {
        raw_rx(dev, start, len);
    } else if (len <= sizeof linear_buf) {
        memcpy(linear_buf, start, before);
        skb_copy_bits(skb, 0, linear_buf + before, (int)skb->len);
        raw_rx(dev, linear_buf, len);
    }
    kfree_skb(skb);
}

static void raw_link_adapter(struct net_device* dev, int up) { if (raw_link) raw_link(dev, up); }

void brys_net_set_handlers(brys_net_rx_fn rx, brys_net_link_fn link) {
    raw_rx = rx;
    raw_link = link;
    brys_netdev_set_rx(rx ? raw_rx_adapter : NULL);
    brys_netdev_set_link(raw_link_adapter);
}
int brys_net_count(void) { return brys_netdev_count(); }
void* brys_net_get(int i) { return brys_netdev_get(i); }
const char* brys_net_name(void* dev) { return ((struct net_device*)dev)->name; }
void brys_net_mac(void* dev, unsigned char mac[6]) { memcpy(mac, ((struct net_device*)dev)->dev_addr, ETH_ALEN); }
unsigned brys_net_mtu(void* dev) { return ((struct net_device*)dev)->mtu; }
int brys_net_link_up(void* dev) { return netif_running(dev) && netif_carrier_ok(dev); }
int brys_net_xmit(void* dev, const void* frame, unsigned len) { return brys_netdev_xmit(dev, frame, len); }
void brys_net_stats(void* dev, unsigned long* rx_packets, unsigned long* tx_packets, unsigned long* rx_bytes,
                    unsigned long* tx_bytes, unsigned long* dropped) {
    const struct net_device_stats* s = &((struct net_device*)dev)->stats;
    *rx_packets = s->rx_packets;
    *tx_packets = s->tx_packets;
    *rx_bytes = s->rx_bytes;
    *tx_bytes = s->tx_bytes;
    *dropped = s->rx_dropped + s->tx_dropped;
}

/* ------------------------------------------------------------------------------------
 * Sumas de verificación de Internet (RFC 1071)
 * ------------------------------------------------------------------------------------ */
__wsum csum_partial(const void* buf, int len, __wsum sum) {
    const u8* p = buf;
    u64 s = sum;
    while (len > 1) { s += (u32)(p[0] | (p[1] << 8)); p += 2; len -= 2; }
    if (len) s += p[0];
    while (s >> 32) s = (s & 0xffffffff) + (s >> 32);
    return (__wsum)s;
}

__sum16 csum_fold(__wsum csum) {
    u32 s = csum;
    s = (s & 0xffff) + (s >> 16);
    s = (s & 0xffff) + (s >> 16);
    return (__sum16)~s;
}

__sum16 ip_fast_csum(const void* iph, unsigned int ihl) { return csum_fold(csum_partial(iph, (int)ihl * 4, 0)); }

__wsum csum_tcpudp_nofold(__be32 saddr, __be32 daddr, u32 len, u8 proto, __wsum sum) {
    u64 s = sum;
    s += saddr;
    s += daddr;
    s += cpu_to_be16((u16)len);
    s += (u32)proto << 8;
    while (s >> 32) s = (s & 0xffffffff) + (s >> 32);
    return (__wsum)s;
}

__sum16 csum_ipv6_magic(const struct in6_addr* saddr, const struct in6_addr* daddr, u32 len, u8 proto, __wsum sum) {
    u64 s = sum;
    for (int i = 0; i < 4; i++) s += saddr->s6_addr32[i] + (u64)daddr->s6_addr32[i];
    s += cpu_to_be32(len);
    s += (u32)proto << 24;
    while (s >> 32) s = (s & 0xffffffff) + (s >> 32);
    return csum_fold((__wsum)s);
}

/* Nota al terminar este archivo:
 * Un sk_buff es la estructura más querida y más temida de Linux: head, data, tail, end y una
 * skb_shared_info escondida al final del búfer, como un mensaje dentro de una botella. Aquí
 * cada búfer vive en su propia página para que la tarjeta pueda escribir en él por DMA sin
 * que nadie le pregunte dónde está la memoria. — Claude, octubre de 2026 */

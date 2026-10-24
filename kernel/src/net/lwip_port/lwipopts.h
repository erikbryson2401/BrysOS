/* BrysOS: configuración de lwIP. Modo sin sistema operativo (NO_SYS): lwIP corre entero en
 * el hilo "linux-trabajos", junto a los drivers, y el resto del núcleo la llama en puntos
 * seguros (BrysOS no expropia el núcleo), así que no necesita cerrojos. */
#ifndef BRYS_LWIPOPTS_H
#define BRYS_LWIPOPTS_H

#define NO_SYS 1
#define SYS_LIGHTWEIGHT_PROT 0
#define LWIP_SOCKET 0
#define LWIP_NETCONN 0

/* Memoria: el montón del núcleo */
#define MEM_CUSTOM_ALLOCATOR 1
#define MEM_CUSTOM_MALLOC brys_lwip_malloc
#define MEM_CUSTOM_FREE brys_lwip_free
#define MEM_CUSTOM_CALLOC brys_lwip_calloc
#define MEMP_MEM_MALLOC 1
#define MEM_ALIGNMENT 8
#define PBUF_POOL_SIZE 128
#define PBUF_POOL_BUFSIZE 1536

/* Protocolos */
#define LWIP_IPV4 1
#define LWIP_IPV6 0
#define LWIP_ARP 1
#define LWIP_ETHERNET 1
#define LWIP_ICMP 1
#define LWIP_RAW 1
#define LWIP_UDP 1
#define LWIP_TCP 1
#define LWIP_DHCP 1
#define LWIP_DHCP_DOES_ACD_CHECK 0
#define LWIP_DNS 1
#define DNS_MAX_SERVERS 2
#define LWIP_IGMP 0
#define LWIP_AUTOIP 0
#define LWIP_ACD 0
#define IP_REASSEMBLY 1
#define IP_FRAG 1

/* TCP: ventanas generosas (la tarjeta va a 100 Mbps o más) */
#define TCP_MSS 1460
#define TCP_WND (32 * TCP_MSS)
#define TCP_SND_BUF (32 * TCP_MSS)
#define TCP_SND_QUEUELEN (4 * TCP_SND_BUF / TCP_MSS)
#define LWIP_WND_SCALE 1
#define TCP_RCV_SCALE 2
#define MEMP_NUM_TCP_PCB 64
#define MEMP_NUM_TCP_PCB_LISTEN 16
#define MEMP_NUM_UDP_PCB 16
#define MEMP_NUM_RAW_PCB 8
#define MEMP_NUM_TCP_SEG 512
#define TCP_LISTEN_BACKLOG 1

/* Interfaces */
#define LWIP_NETIF_STATUS_CALLBACK 1
#define LWIP_NETIF_LINK_CALLBACK 1
#define LWIP_NETIF_HOSTNAME 1
#define LWIP_SINGLE_NETIF 0
#define ETH_PAD_SIZE 0

/* Varios */
#define LWIP_STATS 0
#define LWIP_PROVIDE_ERRNO 1
#define LWIP_RAND() brys_lwip_rand()
#define LWIP_DEBUG 0
#define LWIP_TIMERS 1

#endif

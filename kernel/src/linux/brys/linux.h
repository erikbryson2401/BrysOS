/* Capa de compatibilidad con Linux: lo que el resto de BrysOS (C++) puede llamar */
#ifndef BRYS_LINUX_H
#define BRYS_LINUX_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Arranca la capa: llama a las initcalls de los drivers portados (sólo la primera vez).
 * Devuelve 1 si las ha llamado ahora y 0 si ya estaba en marcha. */
int brys_linux_start(void);

/* Informe de las tarjetas Broadcom encontradas por ssb (chip, núcleos internos, SPROM),
 * en texto con saltos de línea \r\n. Devuelve los bytes escritos. */
size_t brys_ssb_report(char* buf, size_t size);

/* Hilo "linux-trabajos" (brys/work.c): temporizadores, colas de trabajo, tasklets y NAPI */
int brys_linux_start_worker(void);
void brys_linux_kick(void);
/* Función que el hilo llama en cada vuelta y, como mínimo, cada 'interval_ms' (la pila
 * TCP/IP: sus temporizadores) */
void brys_linux_set_poll_hook(void (*fn)(void), unsigned interval_ms);

/* Tarjetas de red de los drivers de Linux, vistas sin tipos de Linux (para la pila TCP/IP).
 * 'dev' es un struct net_device*. Las funciones de recepción y de enlace se llaman desde el
 * hilo "linux-trabajos". */
typedef void (*brys_net_rx_fn)(void* dev, const void* frame, unsigned len);
typedef void (*brys_net_link_fn)(void* dev, int up);
void brys_net_set_handlers(brys_net_rx_fn rx, brys_net_link_fn link);
int brys_net_count(void);
void* brys_net_get(int i);
const char* brys_net_name(void* dev);
void brys_net_mac(void* dev, unsigned char mac[6]);
unsigned brys_net_mtu(void* dev);
int brys_net_link_up(void* dev);
int brys_net_xmit(void* dev, const void* frame, unsigned len);
void brys_net_stats(void* dev, unsigned long* rx_packets, unsigned long* tx_packets, unsigned long* rx_bytes,
                    unsigned long* tx_bytes, unsigned long* dropped);

#ifdef __cplusplus
}
#endif

#endif

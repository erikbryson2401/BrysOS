/* Pila TCP/IP de BrysOS (lwIP sobre los drivers de Linux): lo que usa el resto del núcleo */
#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Pone en marcha lwIP con las tarjetas que hayan registrado los drivers (una vez).
 * Devuelve el número de interfaces. */
int brys_stack_start(void);
/* Cambia el servidor DNS principal (dirección IPv4 escrita). 0 si es válida. */
int brys_stack_set_dns(const char* server);
/* 1 si alguna interfaz tiene ya dirección (DHCP) */
int brys_stack_has_ip(void);
/* Informe de las interfaces (MAC, enlace, IP, DNS), con saltos de línea \r\n */
size_t brys_stack_report(char* buf, size_t size);
/* Resuelve un nombre (o una dirección escrita) a IPv4 en orden de red. 0 si lo consigue. */
int brys_stack_resolve(const char* host, uint32_t* ip_be, unsigned timeout_ms);
/* Un eco ICMP. 0 si llega la respuesta, con su tiempo y su TTL. */
int brys_stack_ping(uint32_t ip_be, unsigned seq, unsigned timeout_ms, unsigned* rtt_ms, unsigned* ttl);

#ifdef __cplusplus
}
#endif

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
/* Servidor DNS número i (0 o 1) escrito ("8.8.8.8"). 1 si existe. */
int brys_stack_dns_server(int i, char* out, unsigned cap);

/* Sockets (net/socket.c) para los programas de Linux: TCP (tipo 1) y UDP (tipo 2) sobre
 * IPv4. Devuelven 0 o un número de bytes, o un error de Linux en negativo. */
int brys_socket_create(int domain, int type, int proto, void** out);
void brys_socket_close(void* sock);
int brys_socket_bind(void* sock, const void* addr, unsigned len);
int brys_socket_listen(void* sock, int backlog);
int brys_socket_accept(void* sock, void** out, int nonblock);
int brys_socket_connect(void* sock, const void* addr, unsigned len, int nonblock);
long brys_socket_sendto(void* sock, const void* buf, uint64_t len, const void* addr, unsigned alen, int nonblock);
long brys_socket_send(void* sock, const void* buf, uint64_t len, int nonblock);
long brys_socket_recvfrom(void* sock, void* buf, uint64_t len, int nonblock, void* addr, uint32_t* alen);
long brys_socket_recv(void* sock, void* buf, uint64_t len, int nonblock);
int brys_socket_poll(void* sock);       /* POLLIN 1, POLLOUT 4, POLLERR 8, POLLHUP 0x10 */
int brys_socket_available(void* sock);  /* bytes que se pueden leer ya */
int brys_socket_shutdown(void* sock, int how);
int brys_socket_name(void* sock, int peer, void* addr, unsigned cap);
int brys_socket_setopt(void* sock, int level, int name, const void* val, unsigned len);
int brys_socket_getopt(void* sock, int level, int name, void* val, unsigned* len);

#ifdef __cplusplus
}
#endif

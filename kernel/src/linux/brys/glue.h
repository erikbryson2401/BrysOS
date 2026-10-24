/* Capa de compatibilidad con Linux: funciones de BrysOS (C++, brys/glue.cpp) que usa el
 * código portado de Linux (C). Es la única frontera entre los dos mundos. */
#ifndef BRYS_GLUE_H
#define BRYS_GLUE_H

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Registro */
void brys_log(const char* text);
void brys_panic(const char* text) __attribute__((noreturn));

/* Memoria del núcleo */
void* brys_kmalloc(size_t size);
void* brys_krealloc(void* p, size_t size);
void brys_kfree(void* p);
/* Memoria para DMA: páginas físicas contiguas (por debajo de 4 GiB), a cero */
void* brys_dma_alloc(size_t size, uint64_t* phys);
void brys_dma_free(void* virt, size_t size);
uint64_t brys_virt_to_phys(const void* virt);
void* brys_phys_to_virt(uint64_t phys);
void* brys_ioremap(uint64_t phys, size_t size);

/* Tiempo */
uint64_t brys_ticks_ms(void);          /* milisegundos desde el arranque */
uint64_t brys_ns(void);                /* nanosegundos (TSC calibrado) */
void brys_udelay(unsigned long us);    /* espera activa */
void brys_sleep_ms(unsigned long ms);  /* duerme (cede el procesador) */

/* Hilos e interrupciones */
void brys_yield(void);
void* brys_current_thread(void);
int brys_kthread(void (*fn)(void*), void* arg, const char* name);
void brys_wait(int (*cond)(void*), void* ctx);   /* duerme hasta que cond(ctx) != 0 */
unsigned long brys_irq_save(void);
void brys_irq_restore(unsigned long flags);
int brys_request_irq(int irq, void (*fn)(void*), void* ctx);
void brys_free_irq(int irq, void (*fn)(void*), void* ctx);

/* PCI (índices de la lista de dispositivos de BrysOS) */
struct brys_pci_info {
    uint8_t bus, dev, func;
    uint16_t vendor, device;
    uint16_t subvendor, subdevice;
    uint8_t cls, subclass, prog_if, rev;
    uint8_t irq;
    int is_pcie;
};
int brys_pci_count(void);
int brys_pci_info(int index, struct brys_pci_info* out);
uint32_t brys_pci_read(int index, int off, int size);
void brys_pci_write(int index, int off, int size, uint32_t value);
/* Dirección física y tamaño de un BAR de memoria (0 si no lo es) */
uint64_t brys_pci_bar(int index, int bar, uint64_t* size);
/* Puerto y tamaño de un BAR de E/S (0 si no lo es) */
uint32_t brys_pci_bar_io(int index, int bar, uint32_t* size);

/* Archivos (para cargar firmware): devuelve un búfer de brys_kmalloc o NULL */
void* brys_read_file(const char* path, size_t* size);

#ifdef __cplusplus
}
#endif

#endif

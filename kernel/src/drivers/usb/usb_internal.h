#pragma once
#include <stdint.h>
#include <stddef.h>
#include "drivers/pci.h"

namespace usb {
enum Speed { LOW, FULL, HIGH };

struct [[gnu::packed]] SetupPacket {
    uint8_t type, request;
    uint16_t value, index, length;
};

class Controller;

struct Device {
    Controller* hc = nullptr;
    int port = 0;
    Speed speed = FULL;
    uint8_t address = 0;
    uint16_t mps0 = 8;
    bool present = true;
    uint16_t vendor = 0, product = 0;
    // HID en modo boot
    int kind = 0;            // 0 ninguno, 1 teclado, 2 ratón
    uint8_t iface = 0;
    uint8_t ep = 0;
    uint16_t ep_mps = 8;
    uint8_t interval = 10;
    void* pipe = nullptr;
    uint8_t last[8] = {};
    char desc[64] = {};
    bool seen = false;          // ya ha enviado algún informe
    // concentradores
    Device* parent = nullptr;   // concentrador al que está conectado (nullptr = puerto raíz)
    int hub_port = 0;           // puerto del concentrador (1..n)
    bool is_hub = false;
    int hub_ports = 0;
    void* storage = nullptr;    // disco de almacenamiento masivo (msd.cpp)
};

class Controller {
public:
    const char* kind = "";
    uint8_t next_address = 1;
    virtual ~Controller() {}
    virtual int port_count() = 0;
    virtual bool port_connected(int port) = 0;
    virtual bool port_changed(int port) = 0;          // lee y borra el indicador de cambio
    virtual bool port_reset(int port, Speed& speed) = 0;
    // Transferencia de control síncrona; 'data' tiene s.length bytes
    virtual bool control(Device& d, const SetupPacket& s, void* data) = 0;
    // Transferencia bulk síncrona (como mucho 32 KiB). 'toggle' es el bit DATA0/DATA1 del
    // punto final y se actualiza. Devuelve los bytes transferidos, -1 si hubo error y -2 si
    // el punto final quedó detenido (STALL).
    virtual int bulk(Device& d, uint8_t ep, bool in, void* data, uint32_t len, uint8_t& toggle, uint16_t mps) = 0;
    virtual void* open_interrupt(Device& d) = 0;
    // Bytes recibidos (>= 0) o -1 si todavía no hay datos
    virtual int poll_interrupt(void* pipe, uint8_t* out, int max) = 0;
    virtual void close_interrupt(void* pipe) = 0;
    // EHCI: sólo atiende dispositivos de alta velocidad (los demás van a las compañeras)
    virtual bool high_speed_only() const { return false; }
    // Tamaño máximo de una transferencia bulk
    virtual uint32_t max_bulk() const { return 32768; }
    // EHCI: cede un puerto raíz a la controladora compañera (teclados y ratones)
    virtual bool release_port(int port) { return false; }
};

// Memoria para DMA por debajo de 4 GiB (nunca se libera)
void* dma_alloc(size_t size, size_t align, uint32_t& phys);
void sleep(uint32_t ms);

void add_controller(Controller* c);
Controller* uhci_create(const PciDevice& d);
Controller* ohci_create(const PciDevice& d);
Controller* ehci_create(const PciDevice& d);

// Almacenamiento masivo (clase 8, SCSI transparente, Bulk-Only)
bool msd_attach(Device& d, uint8_t iface, uint8_t ep_in, uint16_t mps_in, uint8_t ep_out, uint16_t mps_out);
void msd_detach(Device& d);

void hid_report(Device& d, const uint8_t* data, int len);
void hid_tick();
void hid_set_leds(bool caps);
}

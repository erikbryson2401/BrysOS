#pragma once
// Intérprete de AML (ACPI Machine Language): carga la DSDT y las SSDT en un espacio de nombres
// y ejecuta sus métodos (_STA, _BST, _BCM...). El acceso al hardware (memoria, puertos, PCI,
// controlador empotrado) lo da el sistema con aml::Host, así que también se compila en Linux
// para las pruebas (tools/amltest_main.cpp).
#include <stdint.h>
#include <stddef.h>

namespace aml {

// Tipos (los valores de 0 a 16 son los de ObjectType)
enum Type : uint8_t {
    UNINIT = 0, INTEGER = 1, STRING = 2, BUFFER = 3, PACKAGE = 4, FIELD = 5, DEVICE = 6, EVENT = 7,
    METHOD = 8, MUTEX = 9, REGION = 10, POWER = 11, PROCESSOR = 12, THERMAL = 13, BUFFER_FIELD = 14,
    DEBUG_OBJ = 16,
    SCOPE = 0x80, REFERENCE = 0x81
};

// Espacios de direcciones de las OperationRegion
enum Space : uint8_t { MEMORY = 0, IO = 1, PCI = 2, EC = 3, SMBUS = 4, CMOS = 5, PCI_BAR = 6 };

struct Node;
struct Object;

typedef Object* (*NativeMethod)(Object** args, int argc);

struct Object {
    uint32_t refs;
    Type type;
    union {
        uint64_t integer;
        struct { char* ptr; uint32_t len; } string;           // termina en 0
        struct { uint8_t* ptr; uint32_t len; } buffer;
        struct { Object** items; uint32_t count; } package;   // un elemento puede ser nullptr (sin valor)
        struct { const uint8_t* code; uint32_t len; uint8_t flags; NativeMethod native; } method;
        struct {
            uint8_t space;
            uint64_t base, length;
            Node* node;
            bool pci_ready, pci_busy;   // dirección PCI ya calculada / calculándose
            uint32_t seg, bus, dev, fn;
        } region;
        struct {
            uint8_t kind;          // 0 normal, 1 IndexField, 2 BankField
            uint8_t access, lock, update;
            Node* region;          // normal y BankField
            Node* index;           // IndexField
            Node* data;
            Node* bank;            // BankField
            uint64_t bank_value;
            uint64_t bit_offset;
            uint32_t bit_length;
        } field;
        struct { Object* buffer; uint64_t bit_offset; uint32_t bit_length; } buffer_field;
        struct {
            uint8_t kind;          // REF_NODE, REF_INDEX, REF_SLOT
            Node* node;
            Object* container;     // paquete, búfer o cadena (REF_INDEX)
            uint32_t index;
            Object** slot;         // LocalX / ArgX (REF_SLOT)
        } reference;
        struct { uint64_t count; } event;
    };
};

enum { REF_NODE = 1, REF_INDEX = 2, REF_SLOT = 3 };

// Acceso al hardware. Los anchos son de 8, 16, 32 o 64 bits; en el controlador empotrado
// (EC) siempre de 8.
struct Host {
    uint64_t (*read)(uint8_t space, uint64_t address, int bits);
    void (*write)(uint8_t space, uint64_t address, int bits, uint64_t value);
    uint64_t (*pci_read)(uint32_t seg, uint32_t bus, uint32_t dev, uint32_t fn, uint32_t offset, int bits);
    void (*pci_write)(uint32_t seg, uint32_t bus, uint32_t dev, uint32_t fn, uint32_t offset, int bits, uint64_t value);
    void (*sleep_ms)(uint32_t ms);
    void (*stall_us)(uint32_t us);
    uint64_t (*timer_100ns)();
    void (*notify)(Node* node, uint64_t value);
    bool (*osi)(const char* feature);   // respuesta a _OSI
};

bool init(const Host& h);
// Carga una tabla (DSDT o SSDT) completa, con su cabecera. La memoria de la tabla debe
// seguir existiendo (los métodos se ejecutan desde ella).
bool load_table(const uint8_t* table, uint32_t length);
// _REG(espacio, 1) en los dispositivos con regiones de ese espacio
void run_reg(uint8_t space);
// \_SB._INI y _INI de los dispositivos presentes (según _STA)
void init_devices();

// "\\_SB.PCI0.LPC.EC0" (absoluta) o relativa a 'scope' ("_STA")
Node* lookup(const char* path, Node* scope = nullptr);
// Evalúa un objeto o ejecuta un método. Devuelve una referencia nueva (liberar con unref)
// o nullptr si no existe o falla.
Object* evaluate(Node* scope, const char* path, Object** args = nullptr, int argc = 0);
bool eval_int(Node* scope, const char* path, uint64_t& out);
bool exists(Node* scope, const char* path);

Object* make_int(uint64_t v);
Object* make_string(const char* s);
void ref(Object* o);
void unref(Object* o);
// Elemento de un paquete como entero (false si no lo es)
bool pkg_int(Object* pkg, uint32_t i, uint64_t& out);
const char* pkg_string(Object* pkg, uint32_t i);

Node* root();
Node* parent(Node* n);
Object* node_object(Node* n);
uint32_t node_name(Node* n);
void node_path(Node* n, char* buf, size_t cap);
// Recorre el espacio de nombres en profundidad; 'cb' devuelve false para no entrar en los hijos
void walk(Node* from, bool (*cb)(Node* n, void* ctx), void* ctx);
// ¿Coincide _HID o _CID con "PNP0C0A"?
bool device_is(Node* dev, const char* id);

struct Stats {
    uint32_t nodes, methods, tables, errors;
    uint32_t int_bits;
};
Stats stats();
// Últimos errores del intérprete (para la orden de diagnóstico)
const char* last_error();

}

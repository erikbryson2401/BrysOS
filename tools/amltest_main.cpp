// Prueba (Linux) del intérprete de AML: carga las tablas con el hardware simulado en memoria
// (como acpiexec de ACPICA: lo que se escribe se puede volver a leer, todo empieza a cero),
// ejecuta _REG y _INI y evalúa los métodos pedidos.
// Uso: amltest [-e espacio:dirección=valor ...] tabla.dat... -- ruta... | --all
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <string>
#include <vector>
#include "acpi/aml.h"

static std::map<std::pair<int, uint64_t>, uint8_t> mem;   // (espacio, dirección) -> byte

static bool smbus_done = false;   // --smbus: el EC termina al momento las órdenes SMBus

static uint64_t rd(uint8_t space, uint64_t a, int bits) {
    if (smbus_done && space == 3 && a == 1 && bits == 8) return 0x80;   // SMST: hecho
    uint64_t v = 0;
    for (int i = 0; i < bits / 8; i++) {
        auto it = mem.find({space, a + i});
        if (it != mem.end()) v |= (uint64_t)it->second << (8 * i);
    }
    return v;
}
static void wr(uint8_t space, uint64_t a, int bits, uint64_t v) {
    for (int i = 0; i < bits / 8; i++) mem[{space, a + i}] = (uint8_t)(v >> (8 * i));
}
static uint64_t pci_key(uint32_t s, uint32_t b, uint32_t d, uint32_t f, uint32_t o) {
    return ((uint64_t)s << 40) | ((uint64_t)b << 32) | ((uint64_t)d << 24) | ((uint64_t)f << 16) | o;
}
static uint64_t prd(uint32_t s, uint32_t b, uint32_t d, uint32_t f, uint32_t o, int bits) { return rd(2, pci_key(s, b, d, f, o), bits); }
static void pwr(uint32_t s, uint32_t b, uint32_t d, uint32_t f, uint32_t o, int bits, uint64_t v) { wr(2, pci_key(s, b, d, f, o), bits, v); }
static void nop_sleep(uint32_t) {}
static uint64_t timer() { return 0; }
static void notify(aml::Node* n, uint64_t v) {
    char p[128];
    aml::node_path(n, p, sizeof p);
    printf("NOTIFY %s %llx\n", p, (unsigned long long)v);
}
// Lo mismo que responde acpiexec
static bool osi(const char* s) {
    static const char* yes[] = {"Windows 2000", "Windows 2001", "Windows 2001 SP1", "Windows 2001.1", "Windows 2001 SP2",
                                "Windows 2001.1 SP1", "Windows 2006", "Windows 2006.1", "Windows 2006 SP1", "Windows 2006 SP2",
                                "Windows 2009", "Windows 2012", "Windows 2013", "Windows 2015", "Windows 2016", "Windows 2017",
                                "Windows 2017.2", "Windows 2018", "Windows 2018.2", "Windows 2019", "Windows 2020", "Windows 2021",
                                "Windows 2022", "Module Device", "Processor Device", "3.0 Thermal Model", "3.0 _SCP Extensions",
                                "Processor Aggregator Device", "Extended Address Space Descriptor", nullptr};
    for (int i = 0; yes[i]; i++)
        if (!strcmp(s, yes[i])) return true;
    return false;
}

static void print(aml::Object* o, int indent) {
    printf("%*s", indent, "");
    if (!o) { printf("(nada)\n"); return; }
    switch (o->type) {
    case aml::INTEGER: printf("Integer %llX\n", (unsigned long long)o->integer); break;
    case aml::STRING: printf("String \"%s\"\n", o->string.ptr); break;
    case aml::BUFFER:
        printf("Buffer %u:", o->buffer.len);
        for (uint32_t i = 0; i < o->buffer.len; i++) printf(" %02X", o->buffer.ptr[i]);
        printf("\n");
        break;
    case aml::PACKAGE:
        printf("Package %u\n", o->package.count);
        for (uint32_t i = 0; i < o->package.count; i++) print(o->package.items[i], indent + 2);
        break;
    case aml::REFERENCE:
        if (o->reference.kind == aml::REF_NODE) {
            char p[128];
            aml::node_path(o->reference.node, p, sizeof p);
            printf("Reference %s\n", p);
        } else printf("Reference\n");
        break;
    default: printf("Type %u\n", o->type);
    }
}

int main(int argc, char** argv) {
    std::vector<std::string> tables, paths;
    bool all = false, after = false;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--") { after = true; continue; }
        if (a == "--all") { all = true; continue; }
        if (a == "--smbus") { smbus_done = true; continue; }
        if (a == "-e" && i + 1 < argc) {   // espacio:dirección=valor (un byte)
            unsigned sp;
            unsigned long long ad, v;
            if (sscanf(argv[++i], "%u:%llx=%llx", &sp, &ad, &v) == 3) mem[{(int)sp, ad}] = (uint8_t)v;
            continue;
        }
        (after ? paths : tables).push_back(a);
    }
    aml::Host h{rd, wr, prd, pwr, nop_sleep, nop_sleep, timer, notify, osi};
    aml::init(h);
    std::vector<std::vector<uint8_t>> keep;
    for (auto& t : tables) {
        FILE* f = fopen(t.c_str(), "rb");
        if (!f) { perror(t.c_str()); return 2; }
        std::vector<uint8_t> d;
        int c;
        while ((c = fgetc(f)) != EOF) d.push_back((uint8_t)c);
        fclose(f);
        keep.push_back(d);
        bool ok = aml::load_table(keep.back().data(), (uint32_t)keep.back().size());
        fprintf(stderr, "%s: %s\n", t.c_str(), ok ? "cargada" : "con errores");
    }
    aml::run_reg(aml::EC);
    aml::init_devices();
    auto st = aml::stats();
    fprintf(stderr, "nodos %u, métodos %u, errores %u, enteros de %u bits\n", st.nodes, st.methods, st.errors, st.int_bits);
    if (all) {
        // todos los métodos sin argumentos, en el orden del espacio de nombres
        std::vector<std::string> list;
        aml::walk(aml::root(), [](aml::Node* n, void* c) {
            aml::Object* o = aml::node_object(n);
            // _INI ya se ha ejecutado al iniciar (repetirlo no es lo normal y acpiexec responde
            // entonces a _OSI de otra forma)
            if (o && o->type == aml::METHOD && !o->method.native && (o->method.flags & 7) == 0 &&
                aml::node_name(n) != 0x494E495F) {
                char p[128];
                aml::node_path(n, p, sizeof p);
                ((std::vector<std::string>*)c)->push_back(p);
            }
            return true;
        }, &list);
        for (auto& p : list) paths.push_back(p);
    }
    for (auto& p : paths) {
        aml::Object* o = aml::evaluate(nullptr, p.c_str());
        printf("== %s\n", p.c_str());
        print(o, 2);
        aml::unref(o);
    }
    return 0;
}

// Intérprete de AML (ACPI 6, sección 20): espacio de nombres, métodos, campos de las
// regiones de operación (memoria, puertos, PCI, controlador empotrado), búferes, paquetes,
// referencias y conversiones. Se ejecuta directamente sobre el código de las tablas.
#include "acpi/aml.h"
#include "acpi/aml_port.h"

namespace aml {
struct Node {
    uint32_t name;
    Node* parent;
    Node* child;
    Node* next;
    Node* alias;   // Alias: el nodo al que apunta
    Object* obj;
};

namespace {
Host H;
Node* g_root = nullptr;
uint64_t g_ones = ~0ull;      // 0xFFFFFFFF si la DSDT es de revisión 1 (enteros de 32 bits)
uint32_t g_int_bits = 64;
bool g_width_set = false;
uint32_t g_nodes, g_methods, g_tables, g_errors;
char g_last[256];
Object g_debug_obj = {1u << 30, DEBUG_OBJ, {0}};   // nunca se libera

constexpr int MAX_DEPTH = 40;
// Como ACPICA: un While se corta tras 0xFFFF vueltas o 2 segundos (un firmware que espera a
// un hardware que no contesta no debe bloquear el sistema)
constexpr uint32_t MAX_LOOPS = 0xFFFF;
constexpr uint64_t MAX_LOOP_TIME = 2 * 10000000ull;   // en unidades de 100 ns

enum Flow { F_NEXT, F_RETURN, F_BREAK, F_CONTINUE, F_ERROR };

struct Frame {
    Object* args[7];
    Object* locals[8];
    Node* scope;
    Node* method;            // nullptr al cargar una tabla
    Object* ret;
    bool err;
    int depth;
    const uint8_t* limit;    // fin del código (método o tabla)
    Node** created;          // nombres creados por el método (se borran al salir)
    int ncreated, capcreated;
};

// ------------------------------------------------------------------ utilidades
uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
uint32_t rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
uint64_t rd64(const uint8_t* p) { return rd32(p) | ((uint64_t)rd32(p + 4) << 32); }

Node* real(Node* n) {
    for (int i = 0; n && n->alias && i < 16; i++) n = n->alias;
    return n;
}

void seg_text(uint32_t s, char out[5]) {
    for (int i = 0; i < 4; i++) out[i] = (char)(s >> (i * 8));
    out[4] = 0;
}

void path_of(Node* n, char* buf, size_t cap) {
    if (!cap) return;
    if (!n || n == g_root || !n->parent) { aml_snprintf(buf, cap, "\\"); return; }
    Node* chain[64];
    int k = 0;
    for (Node* x = n; x && x != g_root && k < 64; x = x->parent) chain[k++] = x;
    size_t pos = 0;
    buf[pos++] = '\\';
    for (int i = k - 1; i >= 0 && pos + 6 < cap; i--) {
        char s[5];
        seg_text(chain[i]->name, s);
        for (int j = 0; j < 4; j++) buf[pos++] = s[j];
        if (i) buf[pos++] = '.';
    }
    buf[pos] = 0;
}

void fail(Frame& f, const char* fmt, ...) {
    if (f.err) return;
    f.err = true;
    g_errors++;
    char msg[160];
    va_list ap;
    va_start(ap, fmt);
    aml_vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    char where[96];
    if (f.method) path_of(f.method, where, sizeof where);
    else aml_snprintf(where, sizeof where, "(carga)");
    aml_snprintf(g_last, sizeof g_last, "%s: %s", where, msg);
    if (g_errors <= 40) aml_log("AML: %s\n", g_last);
}

// ------------------------------------------------------------------ objetos
Object* new_obj(Type t) {
    auto* o = (Object*)aml_alloc(sizeof(Object));
    if (!o) return nullptr;
    o->refs = 1;
    o->type = t;
    return o;
}
Object* make_str_n(const char* s, uint32_t len) {
    Object* o = new_obj(STRING);
    o->string.ptr = (char*)aml_alloc(len + 1);
    if (s && len) memcpy(o->string.ptr, s, len);
    o->string.ptr[len] = 0;
    o->string.len = len;
    return o;
}
Object* make_buf(uint32_t len, const uint8_t* src) {
    Object* o = new_obj(BUFFER);
    o->buffer.ptr = (uint8_t*)aml_alloc(len);
    if (src && len) memcpy(o->buffer.ptr, src, len);
    o->buffer.len = len;
    return o;
}
Object* make_pkg(uint32_t n) {
    Object* o = new_obj(PACKAGE);
    o->package.items = (Object**)aml_alloc(sizeof(Object*) * (n ? n : 1));
    o->package.count = n;
    return o;
}
Object* make_ref_node(Node* n) {
    Object* o = new_obj(REFERENCE);
    o->reference.kind = REF_NODE;
    o->reference.node = n;
    return o;
}

Object* copy(Object* o) {
    if (!o) return nullptr;
    switch (o->type) {
    case INTEGER: return make_int(o->integer);
    case STRING: return make_str_n(o->string.ptr, o->string.len);
    case BUFFER: return make_buf(o->buffer.len, o->buffer.ptr);
    case PACKAGE: {
        Object* p = make_pkg(o->package.count);
        for (uint32_t i = 0; i < o->package.count; i++) p->package.items[i] = copy(o->package.items[i]);
        return p;
    }
    default:
        ref(o);
        return o;
    }
}

uint32_t int_bytes() { return g_int_bits / 8; }

bool parse_hex(const char* s, uint32_t len, uint64_t& v, bool explicit_conv) {
    uint32_t i = 0;
    while (i < len && (s[i] == ' ' || s[i] == '\t')) i++;
    int base = explicit_conv ? 10 : 16;
    if (i + 1 < len && s[i] == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X')) { base = 16; i += 2; }
    v = 0;
    for (; i < len; i++) {
        char c = s[i];
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (base == 16 && c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (base == 16 && c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else break;
        v = v * (uint64_t)base + (uint64_t)d;
    }
    v &= g_ones;
    return true;
}

bool to_int(Frame& f, Object* o, uint64_t& v) {
    if (!o) { fail(f, "valor sin inicializar"); return false; }
    switch (o->type) {
    case INTEGER: v = o->integer; return true;
    case STRING: return parse_hex(o->string.ptr, o->string.len, v, false);
    case BUFFER: {
        v = 0;
        uint32_t n = o->buffer.len < int_bytes() ? o->buffer.len : int_bytes();
        for (uint32_t i = 0; i < n; i++) v |= (uint64_t)o->buffer.ptr[i] << (8 * i);
        return true;
    }
    default:
        fail(f, "no se puede convertir a entero (tipo %u)", o->type);
        return false;
    }
}

Object* to_buffer(Frame& f, Object* o) {
    if (!o) { fail(f, "valor sin inicializar"); return nullptr; }
    switch (o->type) {
    case BUFFER: ref(o); return o;
    case INTEGER: {
        uint8_t b[8];
        for (int i = 0; i < 8; i++) b[i] = (uint8_t)(o->integer >> (8 * i));
        return make_buf(int_bytes(), b);
    }
    case STRING: return make_buf(o->string.len ? o->string.len + 1 : 0, (const uint8_t*)o->string.ptr);
    default: fail(f, "no se puede convertir a búfer"); return nullptr;
    }
}

const char HEXU[] = "0123456789ABCDEF";

Object* to_string(Frame& f, Object* o) {
    if (!o) { fail(f, "valor sin inicializar"); return nullptr; }
    switch (o->type) {
    case STRING: ref(o); return o;
    case INTEGER: {
        char t[20];
        int n = (int)int_bytes() * 2;
        for (int i = 0; i < n; i++) t[i] = HEXU[(o->integer >> (4 * (n - 1 - i))) & 15];
        return make_str_n(t, (uint32_t)n);
    }
    case BUFFER: {
        uint32_t len = o->buffer.len ? o->buffer.len * 3 - 1 : 0;
        Object* s = make_str_n(nullptr, len);
        for (uint32_t i = 0; i < o->buffer.len; i++) {
            s->string.ptr[i * 3] = HEXU[o->buffer.ptr[i] >> 4];
            s->string.ptr[i * 3 + 1] = HEXU[o->buffer.ptr[i] & 15];
            if (i + 1 < o->buffer.len) s->string.ptr[i * 3 + 2] = ' ';
        }
        return s;
    }
    default: fail(f, "no se puede convertir a cadena"); return nullptr;
    }
}

// ------------------------------------------------------------------ espacio de nombres
Node* find_child(Node* p, uint32_t name) {
    p = real(p);
    if (!p) return nullptr;
    for (Node* c = p->child; c; c = c->next)
        if (c->name == name) return c;
    return nullptr;
}

Node* new_node(Node* parent, uint32_t name) {
    auto* n = (Node*)aml_alloc(sizeof(Node));
    if (!n) return nullptr;
    n->name = name;
    n->parent = parent;
    if (!parent->child) parent->child = n;
    else {
        Node* c = parent->child;
        while (c->next) c = c->next;
        c->next = n;
    }
    g_nodes++;
    return n;
}

void free_subtree(Node* n) {
    Node* c = n->child;
    while (c) {
        Node* nx = c->next;
        free_subtree(c);
        c = nx;
    }
    unref(n->obj);
    aml_free(n);
    g_nodes--;
}

void remove_node(Node* n) {
    Node* p = n->parent;
    if (!p) return;
    if (p->child == n) p->child = n->next;
    else
        for (Node* c = p->child; c; c = c->next)
            if (c->next == n) { c->next = n->next; break; }
    free_subtree(n);
}

struct NamePath {
    bool root;
    int up;
    int nsegs;
    const uint8_t* segs;
};

bool is_lead(uint8_t c) { return c == '_' || (c >= 'A' && c <= 'Z'); }
bool is_name_start(uint8_t c) { return c == '\\' || c == '^' || c == 0x2E || c == 0x2F || is_lead(c); }

bool parse_name(Frame& f, const uint8_t*& p, NamePath& n) {
    n.root = false;
    n.up = 0;
    if (p >= f.limit) { fail(f, "fin inesperado del código"); return false; }
    if (*p == '\\') { n.root = true; p++; }
    else while (p < f.limit && *p == '^') { n.up++; p++; }
    if (p >= f.limit) { fail(f, "nombre incompleto"); return false; }
    if (*p == 0x2E) { p++; n.nsegs = 2; }
    else if (*p == 0x2F) { p++; n.nsegs = *p++; }
    else if (*p == 0x00) { p++; n.nsegs = 0; }
    else n.nsegs = 1;
    n.segs = p;
    p += 4 * n.nsegs;
    if (p > f.limit) { fail(f, "nombre fuera del código"); return false; }
    return true;
}

void name_text(const NamePath& n, char* buf, size_t cap) {
    size_t k = 0;
    if (n.root && k + 1 < cap) buf[k++] = '\\';
    for (int i = 0; i < n.up && k + 1 < cap; i++) buf[k++] = '^';
    for (int s = 0; s < n.nsegs; s++) {
        if (s && k + 1 < cap) buf[k++] = '.';
        for (int j = 0; j < 4 && k + 1 < cap; j++) buf[k++] = (char)n.segs[s * 4 + j];
    }
    buf[k] = 0;
}

Node* base_of(Node* scope, const NamePath& np) {
    Node* b = np.root ? g_root : scope;
    for (int i = 0; i < np.up && b->parent; i++) b = b->parent;
    return b;
}

Node* resolve(Node* scope, const NamePath& np) {
    Node* b = base_of(scope, np);
    if (np.nsegs == 0) return b;
    if (np.nsegs == 1 && !np.root && !np.up) {
        uint32_t s = rd32(np.segs);
        for (Node* x = b; x; x = x->parent)
            if (Node* c = find_child(x, s)) return c;
        return nullptr;
    }
    Node* n = b;
    for (int i = 0; i < np.nsegs && n; i++) n = find_child(n, rd32(np.segs + i * 4));
    return n;
}

// Los nombres que crea un método se borran al terminar
void track(Frame& f, Node* n) {
    if (!n || !f.method) return;
    if (f.ncreated == f.capcreated) {
        int nc = f.capcreated ? f.capcreated * 2 : 16;
        auto** a = (Node**)aml_alloc(sizeof(Node*) * nc);
        if (!a) return;
        if (f.created) {
            memcpy(a, f.created, sizeof(Node*) * f.ncreated);
            aml_free(f.created);
        }
        f.created = a;
        f.capcreated = nc;
    }
    f.created[f.ncreated++] = n;
}

// Crea (o devuelve, si ya existe) el último segmento del nombre
Node* create(Frame& f, const NamePath& np, bool* existed = nullptr) {
    Node* b = base_of(f.scope, np);
    if (existed) *existed = false;
    if (np.nsegs == 0) return b;
    for (int i = 0; i + 1 < np.nsegs; i++) {
        Node* c = find_child(b, rd32(np.segs + i * 4));
        if (!c) {
            char t[64];
            name_text(np, t, sizeof t);
            fail(f, "no existe el ámbito de %s", t);
            return nullptr;
        }
        b = real(c);
    }
    uint32_t last = rd32(np.segs + (np.nsegs - 1) * 4);
    if (Node* c = find_child(b, last)) {
        if (existed) *existed = true;
        return c;
    }
    Node* n = new_node(real(b), last);
    track(f, n);
    return n;
}

void set_obj(Node* n, Object* o) {
    unref(n->obj);
    n->obj = o;
}

uint32_t pkg_length(const uint8_t*& p) {
    uint8_t b0 = *p++;
    int extra = b0 >> 6;
    if (!extra) return b0 & 0x3F;
    uint32_t len = b0 & 0x0F;
    for (int i = 0; i < extra; i++) len |= (uint32_t)(*p++) << (4 + 8 * i);
    return len;
}

// PkgLength: devuelve el final del bloque
const uint8_t* pkg_end(Frame& f, const uint8_t*& p) {
    const uint8_t* s = p;
    uint32_t len = pkg_length(p);
    const uint8_t* e = s + len;
    if (e > f.limit || e < p) {
        fail(f, "longitud de bloque no válida");
        return p;
    }
    return e;
}

// ------------------------------------------------------------------ declaraciones
Object* eval(Frame& f, const uint8_t*& p);
Flow block(Frame& f, const uint8_t* p, const uint8_t* end);
Object* node_value(Frame& f, Node* n);
Object* invoke(Frame& caller, Node* m, Object** args, int argc);
bool store(Frame& f, struct Target& t, Object* v);
bool write_field(Frame& f, Object* fo, Object* v);
Object* read_field(Frame& f, Object* fo);

// ------------------------------------------------------------------ regiones y campos
bool pci_address(Frame& f, Object* r) {
    if (r->region.pci_ready) return true;
    // _ADR o _BBN pueden leer a su vez esta región: entonces vale 0
    if (r->region.pci_busy) return false;
    r->region.pci_busy = true;
    Node* dev = r->region.node ? r->region.node->parent : nullptr;
    if (!dev) return false;
    // la cadena de dispositivos hasta el puente raíz (PNP0A03 / PNP0A08)
    Node* chain[16];
    int k = 0;
    Node* rootb = nullptr;
    for (Node* x = dev; x && x != g_root && k < 16; x = x->parent) {
        chain[k++] = x;
        if (device_is(x, "PNP0A03") || device_is(x, "PNP0A08")) { rootb = x; break; }
    }
    uint64_t seg = 0, bus = 0, adr = 0;
    if (rootb) {
        eval_int(rootb, "_SEG", seg);
        eval_int(rootb, "_BBN", bus);
    }
    // de arriba abajo: cada puente intermedio da el número de su bus secundario
    for (int i = k - 1; i >= 0; i--) {
        adr = 0;
        eval_int(chain[i], "_ADR", adr);
        if (i == 0) break;
        if (chain[i] == rootb) continue;
        uint32_t d = (uint32_t)(adr >> 16) & 31, fn = (uint32_t)adr & 7;
        uint64_t hdr = H.pci_read ? H.pci_read((uint32_t)seg, (uint32_t)bus, d, fn, 0x0E, 8) : 0;
        if ((hdr & 0x7F) == 1) bus = H.pci_read((uint32_t)seg, (uint32_t)bus, d, fn, 0x19, 8);
    }
    r->region.seg = (uint32_t)seg;
    r->region.bus = (uint32_t)bus;
    r->region.dev = (uint32_t)(adr >> 16) & 31;
    r->region.fn = (uint32_t)adr & 7;
    r->region.pci_ready = true;
    r->region.pci_busy = false;
    (void)f;
    return true;
}

uint64_t region_access(Frame& f, Object* r, uint64_t off, int bits, uint64_t value, bool write) {
    uint8_t sp = r->region.space;
    if (off + (uint64_t)bits / 8 > r->region.length) {
        fail(f, "acceso fuera de la región (desplazamiento %x, %u bits, longitud %x)", (unsigned)off, bits, (unsigned)r->region.length);
        return 0;
    }
    uint64_t addr = r->region.base + off;
    uint64_t mask = bits >= 64 ? ~0ull : ((1ull << bits) - 1);
    switch (sp) {
    case MEMORY:
    case IO:
    case CMOS:
        if (write) { if (H.write) H.write(sp, addr, bits, value & mask); return 0; }
        return H.read ? H.read(sp, addr, bits) & mask : 0;
    case EC: {
        // el controlador empotrado se lee y escribe de byte en byte
        uint64_t v = 0;
        for (int i = 0; i < bits / 8; i++) {
            if (write) { if (H.write) H.write(EC, addr + i, 8, (value >> (8 * i)) & 0xFF); }
            else v |= (H.read ? H.read(EC, addr + i, 8) & 0xFF : 0) << (8 * i);
        }
        return v;
    }
    case PCI:
        if (!pci_address(f, r) || !H.pci_read) return 0;
        if (write) {
            H.pci_write(r->region.seg, r->region.bus, r->region.dev, r->region.fn, (uint32_t)addr, bits, value & mask);
            return 0;
        }
        return H.pci_read(r->region.seg, r->region.bus, r->region.dev, r->region.fn, (uint32_t)addr, bits) & mask;
    default:
        return 0;   // SMBus, IPMI...: no se usan
    }
}

int access_bits(Frame& f, Object* fo) {
    auto& fu = fo->field;
    if (fu.kind != 1) {
        Node* rn = real(fu.region);
        if (rn && rn->obj && rn->obj->type == REGION && rn->obj->region.space == EC) return 8;
    }
    switch (fu.access) {
    case 1: return 8;
    case 2: return 16;
    case 3: return 32;
    case 4: return 64;
    case 0: {
        // AnyAcc: el ancho más pequeño que cubre el campo con un solo acceso
        for (int w = 8; w <= 32; w <<= 1) {
            uint64_t start = fu.bit_offset / w * w;
            if (fu.bit_offset + fu.bit_length <= start + w) return w;
        }
        return 8;
    }
    default: return 8;
    }
    (void)f;
}

bool write_field_int(Frame& f, Node* n, uint64_t v) {
    n = real(n);
    if (!n || !n->obj || n->obj->type != FIELD) { fail(f, "campo índice/banco no válido"); return false; }
    Object* o = make_int(v);
    bool ok = write_field(f, n->obj, o);
    unref(o);
    return ok;
}

uint64_t unit_read(Frame& f, Object* fo, uint64_t byte_off, int bits) {
    auto& fu = fo->field;
    if (fu.kind == 1) {
        if (!write_field_int(f, fu.index, byte_off)) return 0;
        Node* dn = real(fu.data);
        if (!dn || !dn->obj || dn->obj->type != FIELD) { fail(f, "campo de datos no válido"); return 0; }
        Object* v = read_field(f, dn->obj);
        uint64_t x = 0;
        if (v) to_int(f, v, x);
        unref(v);
        return x;
    }
    if (fu.kind == 2 && !write_field_int(f, fu.bank, fu.bank_value)) return 0;
    Node* rn = real(fu.region);
    if (!rn || !rn->obj || rn->obj->type != REGION) { fail(f, "región no válida"); return 0; }
    return region_access(f, rn->obj, byte_off, bits, 0, false);
}

void unit_write(Frame& f, Object* fo, uint64_t byte_off, int bits, uint64_t v) {
    auto& fu = fo->field;
    if (fu.kind == 1) {
        if (!write_field_int(f, fu.index, byte_off)) return;
        write_field_int(f, fu.data, v);
        return;
    }
    if (fu.kind == 2 && !write_field_int(f, fu.bank, fu.bank_value)) return;
    Node* rn = real(fu.region);
    if (!rn || !rn->obj || rn->obj->type != REGION) { fail(f, "región no válida"); return; }
    region_access(f, rn->obj, byte_off, bits, v, true);
}

// Bits [off, off+len) de 'src' a la posición 'dst_off' de 'dst'
void copy_bits(uint8_t* dst, uint64_t dst_off, const uint8_t* src, uint64_t off, uint64_t len) {
    for (uint64_t i = 0; i < len; i++) {
        uint64_t s = off + i, d = dst_off + i;
        uint8_t bit = (src[s / 8] >> (s % 8)) & 1;
        dst[d / 8] = (uint8_t)((dst[d / 8] & ~(1 << (d % 8))) | (bit << (d % 8)));
    }
}

Object* bits_result(const uint8_t* data, uint32_t bitlen) {
    uint32_t nbytes = (bitlen + 7) / 8;
    if (bitlen <= g_int_bits) {
        uint64_t v = 0;
        for (uint32_t i = 0; i < nbytes; i++) v |= (uint64_t)data[i] << (8 * i);
        return make_int(v);
    }
    return make_buf(nbytes, data);
}

Object* read_field(Frame& f, Object* fo) {
    auto& fu = fo->field;
    uint32_t len = fu.bit_length;
    uint32_t nbytes = (len + 7) / 8;
    auto* out = (uint8_t*)aml_alloc(nbytes + 8);
    int w = access_bits(f, fo);
    uint64_t end = fu.bit_offset + len;
    for (uint64_t u = fu.bit_offset / w * w; u < end && !f.err; u += w) {
        uint64_t v = unit_read(f, fo, u / 8, w);
        uint8_t vb[8];
        for (int i = 0; i < 8; i++) vb[i] = (uint8_t)(v >> (8 * i));
        uint64_t from = u > fu.bit_offset ? u : fu.bit_offset;
        uint64_t to = u + w < end ? u + w : end;
        copy_bits(out, from - fu.bit_offset, vb, from - u, to - from);
    }
    Object* r = f.err ? nullptr : bits_result(out, len);
    aml_free(out);
    return r;
}

// Bytes de un valor para escribirlo en un campo de 'nbytes'
uint8_t* value_bytes(Frame& f, Object* v, uint32_t nbytes) {
    auto* d = (uint8_t*)aml_alloc(nbytes + 8);
    if (!v) { fail(f, "valor sin inicializar"); aml_free(d); return nullptr; }
    if (v->type == INTEGER) {
        for (uint32_t i = 0; i < nbytes && i < 8; i++) d[i] = (uint8_t)(v->integer >> (8 * i));
    } else if (v->type == BUFFER) {
        memcpy(d, v->buffer.ptr, v->buffer.len < nbytes ? v->buffer.len : nbytes);
    } else if (v->type == STRING) {
        memcpy(d, v->string.ptr, v->string.len < nbytes ? v->string.len : nbytes);
    } else {
        fail(f, "no se puede escribir este tipo en un campo");
        aml_free(d);
        return nullptr;
    }
    return d;
}

bool write_field(Frame& f, Object* fo, Object* v) {
    auto& fu = fo->field;
    uint32_t len = fu.bit_length;
    uint8_t* d = value_bytes(f, v, (len + 7) / 8);
    if (!d) return false;
    int w = access_bits(f, fo);
    uint64_t end = fu.bit_offset + len;
    for (uint64_t u = fu.bit_offset / w * w; u < end && !f.err; u += w) {
        uint64_t from = u > fu.bit_offset ? u : fu.bit_offset;
        uint64_t to = u + w < end ? u + w : end;
        uint64_t old = 0;
        if (from != u || to != u + w) {
            if (fu.update == 0) old = unit_read(f, fo, u / 8, w);   // Preserve
            else if (fu.update == 1) old = ~0ull;                    // WriteAsOnes
        }
        uint8_t vb[8];
        for (int i = 0; i < 8; i++) vb[i] = (uint8_t)(old >> (8 * i));
        copy_bits(vb, from - u, d, from - fu.bit_offset, to - from);
        uint64_t nv = 0;
        for (int i = 0; i < 8; i++) nv |= (uint64_t)vb[i] << (8 * i);
        unit_write(f, fo, u / 8, w, nv);
    }
    aml_free(d);
    return !f.err;
}

Object* read_buffer_field(Frame& f, Object* bf) {
    Object* b = bf->buffer_field.buffer;
    uint64_t off = bf->buffer_field.bit_offset;
    uint32_t len = bf->buffer_field.bit_length;
    if (!b || b->type != BUFFER || off + len > (uint64_t)b->buffer.len * 8) { fail(f, "campo fuera del búfer"); return nullptr; }
    auto* out = (uint8_t*)aml_alloc((len + 7) / 8 + 8);
    copy_bits(out, 0, b->buffer.ptr, off, len);
    Object* r = bits_result(out, len);
    aml_free(out);
    return r;
}

bool write_buffer_field(Frame& f, Object* bf, Object* v) {
    Object* b = bf->buffer_field.buffer;
    uint64_t off = bf->buffer_field.bit_offset;
    uint32_t len = bf->buffer_field.bit_length;
    if (!b || b->type != BUFFER || off + len > (uint64_t)b->buffer.len * 8) { fail(f, "campo fuera del búfer"); return false; }
    uint8_t* d = value_bytes(f, v, (len + 7) / 8);
    if (!d) return false;
    copy_bits(b->buffer.ptr, off, d, 0, len);
    aml_free(d);
    return true;
}

// ------------------------------------------------------------------ destinos (Target / SuperName)
struct Target {
    enum Kind { NONE, LOCAL, ARG, NODE, REF, DEBUG } kind;
    int idx;
    Node* node;
    Object* ref;
};

bool parse_target(Frame& f, const uint8_t*& p, Target& t) {
    t.kind = Target::NONE;
    t.ref = nullptr;
    t.node = nullptr;
    if (p >= f.limit) { fail(f, "fin inesperado del código"); return false; }
    uint8_t c = *p;
    if (c == 0x00) { p++; return true; }
    if (c >= 0x60 && c <= 0x67) { p++; t.kind = Target::LOCAL; t.idx = c - 0x60; return true; }
    if (c >= 0x68 && c <= 0x6E) { p++; t.kind = Target::ARG; t.idx = c - 0x68; return true; }
    if (c == 0x5B && p + 1 < f.limit && p[1] == 0x31) { p += 2; t.kind = Target::DEBUG; return true; }
    if (is_name_start(c)) {
        NamePath np;
        if (!parse_name(f, p, np)) return false;
        Node* n = real(resolve(f.scope, np));
        if (!n) {
            char tx[64];
            name_text(np, tx, sizeof tx);
            fail(f, "%s no existe", tx);
            return false;
        }
        t.kind = Target::NODE;
        t.node = n;
        return true;
    }
    Object* r = eval(f, p);
    if (!r) return false;
    if (r->type != REFERENCE) {
        unref(r);
        fail(f, "el destino no es una referencia");
        return false;
    }
    t.kind = Target::REF;
    t.ref = r;
    return true;
}

void release_target(Target& t) {
    if (t.ref) unref(t.ref);
    t.ref = nullptr;
}

Object* deref(Frame& f, Object* r) {
    switch (r->reference.kind) {
    case REF_NODE: return node_value(f, r->reference.node);
    case REF_SLOT: {
        Object* o = *r->reference.slot;
        if (!o) { fail(f, "referencia a un valor sin inicializar"); return nullptr; }
        ref(o);
        return o;
    }
    case REF_INDEX: {
        Object* c = r->reference.container;
        uint32_t i = r->reference.index;
        if (c->type == PACKAGE) {
            if (i >= c->package.count) { fail(f, "índice fuera del paquete"); return nullptr; }
            Object* o = c->package.items[i];
            if (!o) { fail(f, "elemento de paquete sin inicializar"); return nullptr; }
            if (o->type == REFERENCE && o->reference.kind == REF_NODE) return node_value(f, o->reference.node);
            ref(o);
            return o;
        }
        if (c->type == BUFFER) {
            if (i >= c->buffer.len) { fail(f, "índice fuera del búfer"); return nullptr; }
            return make_int(c->buffer.ptr[i]);
        }
        if (c->type == STRING) {
            if (i >= c->string.len) { fail(f, "índice fuera de la cadena"); return nullptr; }
            return make_int((uint8_t)c->string.ptr[i]);
        }
        fail(f, "referencia no válida");
        return nullptr;
    }
    }
    fail(f, "referencia no válida");
    return nullptr;
}

Object* target_value(Frame& f, Target& t) {
    switch (t.kind) {
    case Target::LOCAL: {
        Object* o = f.locals[t.idx];
        if (!o) { fail(f, "Local%d sin inicializar", t.idx); return nullptr; }
        ref(o);
        return o;
    }
    case Target::ARG: {
        Object* o = f.args[t.idx];
        if (!o) { fail(f, "Arg%d sin inicializar", t.idx); return nullptr; }
        if (o->type == REFERENCE) return deref(f, o);
        ref(o);
        return o;
    }
    case Target::NODE: return node_value(f, t.node);
    case Target::REF: return deref(f, t.ref);
    default: fail(f, "operando no válido"); return nullptr;
    }
}

bool store_node(Frame& f, Node* n, Object* v) {
    n = real(n);
    Object* o = n->obj;
    if (!v) { fail(f, "valor sin inicializar"); return false; }
    if (!o) { n->obj = copy(v); return true; }
    switch (o->type) {
    case FIELD: return write_field(f, o, v);
    case BUFFER_FIELD: return write_buffer_field(f, o, v);
    case INTEGER: {
        uint64_t x;
        if (!to_int(f, v, x)) return false;
        set_obj(n, make_int(x));
        return true;
    }
    case STRING: {
        Object* s = to_string(f, v);
        if (!s) return false;
        Object* c = copy(s);
        unref(s);
        set_obj(n, c);
        return true;
    }
    case BUFFER: {
        Object* b = to_buffer(f, v);
        if (!b) return false;
        // se copia en el mismo búfer (los CreateField que lo usan siguen valiendo)
        uint32_t n2 = b->buffer.len < o->buffer.len ? b->buffer.len : o->buffer.len;
        if (o->buffer.len == 0) {
            Object* c = copy(b);
            unref(b);
            set_obj(n, c);
            return true;
        }
        memmove(o->buffer.ptr, b->buffer.ptr, n2);
        memset(o->buffer.ptr + n2, 0, o->buffer.len - n2);
        unref(b);
        return true;
    }
    case METHOD:
    case DEVICE:
    case REGION:
    case MUTEX:
    case EVENT:
    case PROCESSOR:
    case THERMAL:
    case POWER:
    case SCOPE:
        fail(f, "no se puede guardar un valor en este objeto");
        return false;
    default:
        set_obj(n, copy(v));
        return true;
    }
}

void store_slot(Object** slot, Object* v) {
    Object* c = (v && v->type == REFERENCE) ? (ref(v), v) : copy(v);
    unref(*slot);
    *slot = c;
}

bool store_ref(Frame& f, Object* r, Object* v) {
    switch (r->reference.kind) {
    case REF_NODE: return store_node(f, r->reference.node, v);
    case REF_SLOT: store_slot(r->reference.slot, v); return true;
    case REF_INDEX: {
        Object* c = r->reference.container;
        uint32_t i = r->reference.index;
        if (c->type == PACKAGE) {
            if (i >= c->package.count) { fail(f, "índice fuera del paquete"); return false; }
            store_slot(&c->package.items[i], v);
            return true;
        }
        uint64_t x;
        if (!to_int(f, v, x)) return false;
        if (c->type == BUFFER) {
            if (i >= c->buffer.len) { fail(f, "índice fuera del búfer"); return false; }
            c->buffer.ptr[i] = (uint8_t)x;
            return true;
        }
        if (c->type == STRING) {
            if (i >= c->string.len) { fail(f, "índice fuera de la cadena"); return false; }
            c->string.ptr[i] = (char)x;
            return true;
        }
        break;
    }
    }
    fail(f, "referencia no válida");
    return false;
}

void debug_print(Object* v) {
    if (!v) return;
    if (v->type == INTEGER) aml_log("AML Debug: %llx\n", (unsigned long long)v->integer);
    else if (v->type == STRING) aml_log("AML Debug: %s\n", v->string.ptr);
    else aml_log("AML Debug: (tipo %u)\n", v->type);
}

bool store(Frame& f, Target& t, Object* v) {
    switch (t.kind) {
    case Target::NONE: return true;
    case Target::DEBUG: debug_print(v); return true;
    case Target::LOCAL: store_slot(&f.locals[t.idx], v); return true;
    case Target::ARG:
        if (f.args[t.idx] && f.args[t.idx]->type == REFERENCE) return store_ref(f, f.args[t.idx], v);
        store_slot(&f.args[t.idx], v);
        return true;
    case Target::NODE: return store_node(f, t.node, v);
    case Target::REF: return store_ref(f, t.ref, v);
    }
    return false;
}

// ------------------------------------------------------------------ evaluación
Object* node_value(Frame& f, Node* n) {
    n = real(n);
    Object* o = n ? n->obj : nullptr;
    if (!o) {
        char t[96];
        path_of(n, t, sizeof t);
        fail(f, "%s no tiene valor", t);
        return nullptr;
    }
    switch (o->type) {
    case FIELD: return read_field(f, o);
    case BUFFER_FIELD: return read_buffer_field(f, o);
    case METHOD: return invoke(f, n, nullptr, 0);
    default:
        ref(o);
        return o;
    }
}

bool int_arg(Frame& f, const uint8_t*& p, uint64_t& v) {
    Object* o = eval(f, p);
    if (!o) return false;
    bool ok = to_int(f, o, v);
    unref(o);
    return ok;
}

Object* int_result(Frame& f, const uint8_t*& p, uint64_t v) {
    Object* r = make_int(v & g_ones);
    Target t;
    if (!parse_target(f, p, t)) { unref(r); return nullptr; }
    bool ok = store(f, t, r);
    release_target(t);
    if (!ok) { unref(r); return nullptr; }
    return r;
}

Object* obj_result(Frame& f, const uint8_t*& p, Object* r) {
    if (!r) return nullptr;
    Target t;
    if (!parse_target(f, p, t)) { unref(r); return nullptr; }
    bool ok = store(f, t, r);
    release_target(t);
    if (!ok) { unref(r); return nullptr; }
    return r;
}

// Comparación de LEqual/LGreater/LLess: <0, 0 o >0
bool compare(Frame& f, const uint8_t*& p, int& res) {
    Object* a = eval(f, p);
    if (!a) return false;
    Object* b = eval(f, p);
    if (!b) { unref(a); return false; }
    bool ok = true;
    if (a->type == STRING || a->type == BUFFER) {
        Object* bb = a->type == STRING ? to_string(f, b) : to_buffer(f, b);
        if (!bb) ok = false;
        else {
            const uint8_t* x = a->type == STRING ? (const uint8_t*)a->string.ptr : a->buffer.ptr;
            const uint8_t* y = a->type == STRING ? (const uint8_t*)bb->string.ptr : bb->buffer.ptr;
            uint32_t lx = a->type == STRING ? a->string.len : a->buffer.len;
            uint32_t ly = a->type == STRING ? bb->string.len : bb->buffer.len;
            uint32_t n = lx < ly ? lx : ly;
            int c = n ? memcmp(x, y, n) : 0;
            res = c ? c : (lx < ly ? -1 : lx > ly ? 1 : 0);
            unref(bb);
        }
    } else {
        uint64_t x, y;
        if (!to_int(f, a, x) || !to_int(f, b, y)) ok = false;
        else res = x < y ? -1 : x > y ? 1 : 0;
    }
    unref(a);
    unref(b);
    return ok;
}

Object* concat(Frame& f, Object* a, Object* b) {
    if (a->type == INTEGER) {
        uint64_t y;
        if (!to_int(f, b, y)) return nullptr;
        uint32_t n = int_bytes();
        Object* r = make_buf(n * 2, nullptr);
        for (uint32_t i = 0; i < n; i++) {
            r->buffer.ptr[i] = (uint8_t)(a->integer >> (8 * i));
            r->buffer.ptr[n + i] = (uint8_t)(y >> (8 * i));
        }
        return r;
    }
    if (a->type == STRING) {
        Object* s = to_string(f, b);
        if (!s) return nullptr;
        Object* r = make_str_n(nullptr, a->string.len + s->string.len);
        memcpy(r->string.ptr, a->string.ptr, a->string.len);
        memcpy(r->string.ptr + a->string.len, s->string.ptr, s->string.len);
        unref(s);
        return r;
    }
    if (a->type == BUFFER) {
        Object* s = to_buffer(f, b);
        if (!s) return nullptr;
        Object* r = make_buf(a->buffer.len + s->buffer.len, nullptr);
        memcpy(r->buffer.ptr, a->buffer.ptr, a->buffer.len);
        memcpy(r->buffer.ptr + a->buffer.len, s->buffer.ptr, s->buffer.len);
        unref(s);
        return r;
    }
    fail(f, "Concatenate con un tipo no válido");
    return nullptr;
}

// Elemento de un paquete: los nombres son referencias, no llamadas
Object* pkg_element(Frame& f, const uint8_t*& p) {
    if (is_name_start(*p)) {
        NamePath np;
        if (!parse_name(f, p, np)) return nullptr;
        Node* n = real(resolve(f.scope, np));
        if (n) {
            // como ACPICA: los datos con nombre se resuelven a su valor; los dispositivos,
            // métodos, etc. quedan como referencias (p. ej. en _PRT)
            Object* o = n->obj;
            if (o && (o->type == INTEGER || o->type == STRING || o->type == BUFFER || o->type == PACKAGE)) return copy(o);
            if (o && (o->type == FIELD || o->type == BUFFER_FIELD)) return node_value(f, n);
            return make_ref_node(n);
        }
        char t[64];
        name_text(np, t, sizeof t);
        return make_str_n(t, (uint32_t)strlen(t));
    }
    return eval(f, p);
}

Object* make_package(Frame& f, const uint8_t*& p, bool var) {
    const uint8_t* end = pkg_end(f, p);
    if (f.err) return nullptr;
    uint64_t n;
    if (var) {
        if (!int_arg(f, p, n)) return nullptr;
    } else {
        n = *p++;
    }
    if (n > 65536) { fail(f, "paquete demasiado grande"); return nullptr; }
    Object* pkg = make_pkg((uint32_t)n);
    for (uint32_t i = 0; p < end && !f.err; i++) {
        Object* e = pkg_element(f, p);
        if (!e) break;
        if (i < n) pkg->package.items[i] = e;
        else unref(e);
    }
    p = end;
    if (f.err) { unref(pkg); return nullptr; }
    return pkg;
}

uint64_t eisa_or_hid(Object* o, char out[16]) {
    out[0] = 0;
    if (!o) return 0;
    if (o->type == STRING) {
        // algunos firmwares ponen "*PNP0303": el asterisco no cuenta
        const char* src = o->string.ptr;
        uint32_t len = o->string.len;
        if (len && *src == '*') { src++; len--; }
        uint32_t i = 0;
        for (; i < 15 && i < len; i++) out[i] = src[i];
        out[i] = 0;
        return 1;
    }
    if (o->type == INTEGER) {
        uint32_t id = (uint32_t)o->integer;
        uint32_t v = ((id & 0xFF) << 24) | ((id & 0xFF00) << 8) | ((id >> 8) & 0xFF00) | (id >> 24);
        out[0] = (char)(((v >> 26) & 0x1F) + 0x40);
        out[1] = (char)(((v >> 21) & 0x1F) + 0x40);
        out[2] = (char)(((v >> 16) & 0x1F) + 0x40);
        for (int i = 0; i < 4; i++) out[3 + i] = HEXU[(v >> (12 - 4 * i)) & 15];
        out[7] = 0;
        return 1;
    }
    return 0;
}

Object* to_hex_string(Frame& f, Object* o) {
    if (o->type == STRING) { ref(o); return o; }
    if (o->type == INTEGER) {
        char t[24];
        int n = (int)int_bytes() * 2;
        t[0] = '0';
        t[1] = 'x';
        for (int i = 0; i < n; i++) t[2 + i] = HEXU[(o->integer >> (4 * (n - 1 - i))) & 15];
        return make_str_n(t, (uint32_t)n + 2);
    }
    if (o->type == BUFFER) {
        uint32_t len = o->buffer.len ? o->buffer.len * 5 - 1 : 0;
        Object* s = make_str_n(nullptr, len);
        for (uint32_t i = 0; i < o->buffer.len; i++) {
            char* d = s->string.ptr + i * 5;
            d[0] = '0';
            d[1] = 'x';
            d[2] = HEXU[o->buffer.ptr[i] >> 4];
            d[3] = HEXU[o->buffer.ptr[i] & 15];
            if (i + 1 < o->buffer.len) d[4] = ',';
        }
        return s;
    }
    fail(f, "ToHexString con un tipo no válido");
    return nullptr;
}

Object* to_dec_string(Frame& f, Object* o) {
    if (o->type == STRING) { ref(o); return o; }
    char t[1024];
    size_t k = 0;
    auto put_dec = [&](uint64_t v) {
        char d[24];
        int n = 0;
        do { d[n++] = (char)('0' + v % 10); v /= 10; } while (v);
        while (n && k + 1 < sizeof t) t[k++] = d[--n];
    };
    if (o->type == INTEGER) put_dec(o->integer);
    else if (o->type == BUFFER) {
        for (uint32_t i = 0; i < o->buffer.len && k + 5 < sizeof t; i++) {
            if (i) t[k++] = ',';
            put_dec(o->buffer.ptr[i]);
        }
    } else {
        fail(f, "ToDecimalString con un tipo no válido");
        return nullptr;
    }
    return make_str_n(t, (uint32_t)k);
}

Node* create_named_field(Frame& f, const uint8_t*& p, Object* fieldobj) {
    NamePath np;
    if (!parse_name(f, p, np)) { unref(fieldobj); return nullptr; }
    bool existed;
    Node* n = create(f, np, &existed);
    if (!n) { unref(fieldobj); return nullptr; }
    set_obj(n, fieldobj);
    return n;
}

Object* create_buffer_field(Frame& f, const uint8_t*& p, int kind) {
    // kind: tamaño en bits (8/16/32/64; 1 = bit) o 0 = CreateField
    Object* src = eval(f, p);
    if (!src) return nullptr;
    if (src->type != BUFFER) {
        unref(src);
        fail(f, "CreateField sobre algo que no es un búfer");
        return nullptr;
    }
    uint64_t idx, bits = 0;
    if (!int_arg(f, p, idx)) { unref(src); return nullptr; }
    if (kind == 0 && !int_arg(f, p, bits)) { unref(src); return nullptr; }
    Object* bf = new_obj(BUFFER_FIELD);
    bf->buffer_field.buffer = src;   // la referencia pasa al campo
    if (kind == 0) {
        bf->buffer_field.bit_offset = idx;
        bf->buffer_field.bit_length = (uint32_t)bits;
    } else if (kind == 1) {
        bf->buffer_field.bit_offset = idx;
        bf->buffer_field.bit_length = 1;
    } else {
        bf->buffer_field.bit_offset = idx * 8;
        bf->buffer_field.bit_length = (uint32_t)kind;
    }
    create_named_field(f, p, bf);
    return nullptr;
}

Object* match_op(Frame& f, const uint8_t*& p) {
    Object* pkg = eval(f, p);
    if (!pkg) return nullptr;
    if (pkg->type != PACKAGE) { unref(pkg); fail(f, "Match sobre algo que no es un paquete"); return nullptr; }
    uint8_t op1 = *p++;
    uint64_t v1, v2, start;
    if (!int_arg(f, p, v1)) { unref(pkg); return nullptr; }
    uint8_t op2 = *p++;
    if (!int_arg(f, p, v2) || !int_arg(f, p, start)) { unref(pkg); return nullptr; }
    auto test = [](uint8_t op, uint64_t e, uint64_t v) {
        switch (op) {
        case 0: return true;
        case 1: return e == v;
        case 2: return e <= v;
        case 3: return e < v;
        case 4: return e >= v;
        case 5: return e > v;
        }
        return false;
    };
    uint64_t res = g_ones;
    for (uint64_t i = start; i < pkg->package.count; i++) {
        Object* e = pkg->package.items[i];
        if (!e || e->type != INTEGER) continue;
        if (test(op1, e->integer, v1) && test(op2, e->integer, v2)) { res = i; break; }
    }
    unref(pkg);
    return make_int(res);
}

Object* call_method(Frame& f, Node* n, const uint8_t*& p) {
    int argc = n->obj->method.flags & 7;
    Object* a[7] = {};
    for (int i = 0; i < argc; i++) {
        a[i] = eval(f, p);
        if (!a[i]) {
            for (int j = 0; j < i; j++) unref(a[j]);
            return nullptr;
        }
    }
    Object* r = invoke(f, n, a, argc);
    for (int i = 0; i < argc; i++) unref(a[i]);
    return r;
}

Object* eval(Frame& f, const uint8_t*& p) {
    if (f.err) return nullptr;
    if (p >= f.limit) { fail(f, "fin inesperado del código"); return nullptr; }
    uint8_t op = *p;
    if (is_name_start(op)) {
        NamePath np;
        if (!parse_name(f, p, np)) return nullptr;
        Node* n = real(resolve(f.scope, np));
        if (!n) {
            char t[64];
            name_text(np, t, sizeof t);
            fail(f, "%s no existe", t);
            return nullptr;
        }
        if (n->obj && n->obj->type == METHOD) return call_method(f, n, p);
        return node_value(f, n);
    }
    if (op >= 0x60 && op <= 0x6E) {
        p++;
        Object* o = op <= 0x67 ? f.locals[op - 0x60] : f.args[op - 0x68];
        if (!o) {
            fail(f, op <= 0x67 ? "Local%d sin inicializar" : "Arg%d sin inicializar", op <= 0x67 ? op - 0x60 : op - 0x68);
            return nullptr;
        }
        ref(o);
        return o;
    }
    p++;
    switch (op) {
    case 0x00: return make_int(0);
    case 0x01: return make_int(1);
    case 0xFF: return make_int(g_ones);
    case 0x0A: { uint64_t v = *p; p += 1; return make_int(v); }
    case 0x0B: { uint64_t v = rd16(p); p += 2; return make_int(v); }
    case 0x0C: { uint64_t v = rd32(p); p += 4; return make_int(v); }
    case 0x0E: { uint64_t v = rd64(p); p += 8; return make_int(v); }
    case 0x0D: {
        const uint8_t* s = p;
        while (p < f.limit && *p) p++;
        Object* o = make_str_n((const char*)s, (uint32_t)(p - s));
        p++;
        return o;
    }
    case 0x11: {   // Buffer
        const uint8_t* end = pkg_end(f, p);
        uint64_t size;
        if (f.err || !int_arg(f, p, size)) return nullptr;
        if (size > (16u << 20)) { fail(f, "búfer demasiado grande"); return nullptr; }
        Object* b = make_buf((uint32_t)size, nullptr);
        uint64_t avail = (uint64_t)(end - p);
        memcpy(b->buffer.ptr, p, avail < size ? avail : size);
        p = end;
        return b;
    }
    case 0x12: return make_package(f, p, false);
    case 0x13: return make_package(f, p, true);
    case 0x70: {   // Store
        Object* v = eval(f, p);
        if (!v) return nullptr;
        Target t;
        if (!parse_target(f, p, t)) { unref(v); return nullptr; }
        bool ok = store(f, t, v);
        release_target(t);
        if (!ok) { unref(v); return nullptr; }
        return v;
    }
    case 0x71: {   // RefOf
        Target t;
        if (!parse_target(f, p, t)) return nullptr;
        Object* r = nullptr;
        if (t.kind == Target::NODE) r = make_ref_node(t.node);
        else if (t.kind == Target::LOCAL || t.kind == Target::ARG) {
            r = new_obj(REFERENCE);
            r->reference.kind = REF_SLOT;
            r->reference.slot = t.kind == Target::LOCAL ? &f.locals[t.idx] : &f.args[t.idx];
        } else if (t.kind == Target::REF) {
            r = t.ref;
            t.ref = nullptr;
        } else fail(f, "RefOf no válido");
        release_target(t);
        return r;
    }
    case 0x72: case 0x74: case 0x77: case 0x79: case 0x7A: case 0x7B: case 0x7C: case 0x7D: case 0x7E: case 0x7F: case 0x85: {
        uint64_t a, b;
        if (!int_arg(f, p, a) || !int_arg(f, p, b)) return nullptr;
        uint64_t r = 0;
        switch (op) {
        case 0x72: r = a + b; break;
        case 0x74: r = a - b; break;
        case 0x77: r = a * b; break;
        case 0x79: r = b >= 64 ? 0 : a << b; break;
        case 0x7A: r = b >= 64 ? 0 : a >> b; break;
        case 0x7B: r = a & b; break;
        case 0x7C: r = ~(a & b); break;
        case 0x7D: r = a | b; break;
        case 0x7E: r = ~(a | b); break;
        case 0x7F: r = a ^ b; break;
        case 0x85:
            if (!b) { fail(f, "división por cero"); return nullptr; }
            r = a % b;
            break;
        }
        return int_result(f, p, r);
    }
    case 0x73: {   // Concatenate
        Object* a = eval(f, p);
        if (!a) return nullptr;
        Object* b = eval(f, p);
        if (!b) { unref(a); return nullptr; }
        Object* r = concat(f, a, b);
        unref(a);
        unref(b);
        return obj_result(f, p, r);
    }
    case 0x75: case 0x76: {   // Increment / Decrement
        Target t;
        if (!parse_target(f, p, t)) return nullptr;
        Object* v = target_value(f, t);
        uint64_t x;
        if (!v || !to_int(f, v, x)) { unref(v); release_target(t); return nullptr; }
        unref(v);
        Object* r = make_int((op == 0x75 ? x + 1 : x - 1) & g_ones);
        bool ok = store(f, t, r);
        release_target(t);
        if (!ok) { unref(r); return nullptr; }
        return r;
    }
    case 0x78: {   // Divide
        uint64_t a, b;
        if (!int_arg(f, p, a) || !int_arg(f, p, b)) return nullptr;
        if (!b) { fail(f, "división por cero"); return nullptr; }
        Object* rem = int_result(f, p, a % b);
        if (!rem) return nullptr;
        unref(rem);
        return int_result(f, p, a / b);
    }
    case 0x80: {   // Not
        uint64_t a;
        if (!int_arg(f, p, a)) return nullptr;
        return int_result(f, p, ~a);
    }
    case 0x81: case 0x82: {   // FindSetLeftBit / FindSetRightBit
        uint64_t a;
        if (!int_arg(f, p, a)) return nullptr;
        uint64_t r = 0;
        if (a) {
            if (op == 0x81) { r = 64; while (!(a >> (r - 1) & 1)) r--; }
            else { r = 1; while (!(a >> (r - 1) & 1)) r++; }
        }
        return int_result(f, p, r);
    }
    case 0x83: {   // DerefOf
        Object* o = eval(f, p);
        if (!o) return nullptr;
        Object* r = nullptr;
        if (o->type == REFERENCE) r = deref(f, o);
        else if (o->type == STRING) {
            Node* n = lookup(o->string.ptr, f.scope);
            if (n) r = node_value(f, n);
            else fail(f, "DerefOf: %s no existe", o->string.ptr);
        } else fail(f, "DerefOf de algo que no es una referencia");
        unref(o);
        return r;
    }
    case 0x84: {   // ConcatenateResTemplate
        Object* a = eval(f, p);
        if (!a) return nullptr;
        Object* b = eval(f, p);
        if (!b) { unref(a); return nullptr; }
        if (a->type != BUFFER || b->type != BUFFER) {
            unref(a);
            unref(b);
            fail(f, "ConcatenateResTemplate sin búferes");
            return nullptr;
        }
        uint32_t la = a->buffer.len >= 2 ? a->buffer.len - 2 : a->buffer.len;
        uint32_t lb = b->buffer.len >= 2 ? b->buffer.len - 2 : b->buffer.len;
        Object* r = make_buf(la + lb + 2, nullptr);
        memcpy(r->buffer.ptr, a->buffer.ptr, la);
        memcpy(r->buffer.ptr + la, b->buffer.ptr, lb);
        r->buffer.ptr[la + lb] = 0x79;
        r->buffer.ptr[la + lb + 1] = 0;
        unref(a);
        unref(b);
        return obj_result(f, p, r);
    }
    case 0x87: {   // SizeOf
        Target t;
        if (!parse_target(f, p, t)) return nullptr;
        Object* v = target_value(f, t);
        release_target(t);
        if (!v) return nullptr;
        uint64_t n = 0;
        if (v->type == STRING) n = v->string.len;
        else if (v->type == BUFFER) n = v->buffer.len;
        else if (v->type == PACKAGE) n = v->package.count;
        else fail(f, "SizeOf de un tipo no válido");
        unref(v);
        return f.err ? nullptr : make_int(n);
    }
    case 0x88: {   // Index
        Object* src = eval(f, p);
        if (!src) return nullptr;
        if (src->type == REFERENCE) {
            Object* d = deref(f, src);
            unref(src);
            if (!d) return nullptr;
            src = d;
        }
        uint64_t idx;
        if (!int_arg(f, p, idx)) { unref(src); return nullptr; }
        if (src->type != PACKAGE && src->type != BUFFER && src->type != STRING) {
            unref(src);
            fail(f, "Index sobre un tipo no válido");
            return nullptr;
        }
        uint32_t count = src->type == PACKAGE ? src->package.count : src->type == BUFFER ? src->buffer.len : src->string.len;
        if (idx >= count) {
            unref(src);
            fail(f, "Index fuera de límites (%u de %u)", (unsigned)idx, count);
            return nullptr;
        }
        Object* r = new_obj(REFERENCE);
        r->reference.kind = REF_INDEX;
        r->reference.container = src;
        r->reference.index = (uint32_t)idx;
        return obj_result(f, p, r);
    }
    case 0x89: return match_op(f, p);
    case 0x8A: return create_buffer_field(f, p, 32);
    case 0x8B: return create_buffer_field(f, p, 16);
    case 0x8C: return create_buffer_field(f, p, 8);
    case 0x8D: return create_buffer_field(f, p, 1);
    case 0x8F: return create_buffer_field(f, p, 64);
    case 0x8E: {   // ObjectType
        Target t;
        if (!parse_target(f, p, t)) return nullptr;
        uint64_t ty = 0;
        if (t.kind == Target::NODE) {
            Object* o = real(t.node)->obj;
            ty = o ? (o->type == SCOPE ? 0 : o->type) : 0;
        } else if (t.kind == Target::DEBUG) ty = 16;
        else {
            Object* o = t.kind == Target::LOCAL ? f.locals[t.idx] : t.kind == Target::ARG ? f.args[t.idx] : nullptr;
            if (t.kind == Target::REF) {
                Object* v = deref(f, t.ref);
                ty = v ? v->type : 0;
                unref(v);
            } else ty = o ? (o->type == REFERENCE ? 0 : o->type) : 0;
        }
        release_target(t);
        return f.err ? nullptr : make_int(ty);
    }
    case 0x90: case 0x91: {   // LAnd / LOr
        uint64_t a, b;
        if (!int_arg(f, p, a) || !int_arg(f, p, b)) return nullptr;
        bool r = op == 0x90 ? (a && b) : (a || b);
        return make_int(r ? g_ones : 0);
    }
    case 0x92: {   // LNot (o LNotEqual / LLessEqual / LGreaterEqual)
        if (p < f.limit && (*p == 0x93 || *p == 0x94 || *p == 0x95)) {
            uint8_t c = *p++;
            int res;
            if (!compare(f, p, res)) return nullptr;
            bool r = c == 0x93 ? res != 0 : c == 0x94 ? res <= 0 : res >= 0;
            return make_int(r ? g_ones : 0);
        }
        uint64_t a;
        if (!int_arg(f, p, a)) return nullptr;
        return make_int(a ? 0 : g_ones);
    }
    case 0x93: case 0x94: case 0x95: {
        int res;
        if (!compare(f, p, res)) return nullptr;
        bool r = op == 0x93 ? res == 0 : op == 0x94 ? res > 0 : res < 0;
        return make_int(r ? g_ones : 0);
    }
    case 0x96: {   // ToBuffer
        Object* a = eval(f, p);
        if (!a) return nullptr;
        Object* r = to_buffer(f, a);
        unref(a);
        if (r && r == a) { Object* c = copy(r); unref(r); r = c; }
        return obj_result(f, p, r);
    }
    case 0x97: case 0x98: {   // ToDecimalString / ToHexString
        Object* a = eval(f, p);
        if (!a) return nullptr;
        Object* r = op == 0x97 ? to_dec_string(f, a) : to_hex_string(f, a);
        unref(a);
        return obj_result(f, p, r);
    }
    case 0x99: {   // ToInteger
        Object* a = eval(f, p);
        if (!a) return nullptr;
        uint64_t v = 0;
        bool ok = a->type == STRING ? parse_hex(a->string.ptr, a->string.len, v, true) : to_int(f, a, v);
        unref(a);
        if (!ok) return nullptr;
        return int_result(f, p, v);
    }
    case 0x9C: {   // ToString(buffer, longitud)
        Object* a = eval(f, p);
        if (!a) return nullptr;
        uint64_t len;
        if (!int_arg(f, p, len)) { unref(a); return nullptr; }
        Object* b = to_buffer(f, a);
        unref(a);
        if (!b) return nullptr;
        uint32_t n = 0;
        while (n < b->buffer.len && n < len && b->buffer.ptr[n]) n++;
        Object* r = make_str_n((const char*)b->buffer.ptr, n);
        unref(b);
        return obj_result(f, p, r);
    }
    case 0x9D: {   // CopyObject
        Object* v = eval(f, p);
        if (!v) return nullptr;
        Target t;
        if (!parse_target(f, p, t)) { unref(v); return nullptr; }
        if (t.kind == Target::NODE) set_obj(t.node, copy(v));
        else if (t.kind == Target::LOCAL) store_slot(&f.locals[t.idx], v);
        else if (t.kind == Target::ARG) store_slot(&f.args[t.idx], v);
        else store(f, t, v);
        release_target(t);
        return v;
    }
    case 0x9E: {   // Mid
        Object* src = eval(f, p);
        if (!src) return nullptr;
        uint64_t idx, len;
        if (!int_arg(f, p, idx) || !int_arg(f, p, len)) { unref(src); return nullptr; }
        Object* r = nullptr;
        if (src->type == STRING || src->type == BUFFER) {
            uint32_t total = src->type == STRING ? src->string.len : src->buffer.len;
            uint64_t s = idx < total ? idx : total;
            uint64_t n = s + len <= total ? len : total - s;
            if (src->type == STRING) r = make_str_n(src->string.ptr + s, (uint32_t)n);
            else r = make_buf((uint32_t)n, src->buffer.ptr + s);
        } else fail(f, "Mid sobre un tipo no válido");
        unref(src);
        return obj_result(f, p, r);
    }
    case 0x5B: {
        uint8_t ext = *p++;
        switch (ext) {
        case 0x12: {   // CondRefOf
            Object* r = nullptr;
            if (is_name_start(*p)) {
                NamePath np;
                if (!parse_name(f, p, np)) return nullptr;
                Node* n = resolve(f.scope, np);
                if (n) r = make_ref_node(n);
            } else {
                Target t;
                if (!parse_target(f, p, t)) return nullptr;
                if (t.kind == Target::LOCAL && f.locals[t.idx]) {
                    r = new_obj(REFERENCE);
                    r->reference.kind = REF_SLOT;
                    r->reference.slot = &f.locals[t.idx];
                } else if (t.kind == Target::ARG && f.args[t.idx]) {
                    r = new_obj(REFERENCE);
                    r->reference.kind = REF_SLOT;
                    r->reference.slot = &f.args[t.idx];
                } else if (t.kind == Target::REF) {
                    r = t.ref;
                    t.ref = nullptr;
                }
                release_target(t);
            }
            Target t;
            if (!parse_target(f, p, t)) { unref(r); return nullptr; }
            if (r) store(f, t, r);
            release_target(t);
            uint64_t res = r ? g_ones : 0;
            unref(r);
            return make_int(res);
        }
        case 0x13: return create_buffer_field(f, p, 0);
        case 0x1F: {   // LoadTable: no se admite
            for (int i = 0; i < 6; i++) {
                Object* o = eval(f, p);
                if (!o) return nullptr;
                unref(o);
            }
            return make_int(0);
        }
        case 0x23: {   // Acquire (un solo hilo: siempre se consigue)
            Target t;
            if (!parse_target(f, p, t)) return nullptr;
            release_target(t);
            p += 2;
            return make_int(0);
        }
        case 0x25: {   // Wait
            Target t;
            if (!parse_target(f, p, t)) return nullptr;
            uint64_t ms;
            if (!int_arg(f, p, ms)) { release_target(t); return nullptr; }
            uint64_t res = g_ones;
            if (t.kind == Target::NODE && t.node->obj && t.node->obj->type == EVENT && t.node->obj->event.count) {
                t.node->obj->event.count--;
                res = 0;
            }
            release_target(t);
            return make_int(res);
        }
        case 0x28: case 0x29: {   // FromBCD / ToBCD
            uint64_t a;
            if (!int_arg(f, p, a)) return nullptr;
            uint64_t r = 0, m = 1;
            if (ext == 0x28) {
                for (int i = 0; i < 16; i++, a >>= 4, m *= 10) r += (a & 15) * m;
            } else {
                for (int i = 0; i < 16 && a; i++, a /= 10) r |= (a % 10) << (4 * i);
            }
            return int_result(f, p, r);
        }
        case 0x30: return make_int(0x20250101);   // Revision
        case 0x31: ref(&g_debug_obj); return &g_debug_obj;
        case 0x33: return make_int(H.timer_100ns ? H.timer_100ns() : 0);
        }
        fail(f, "operación 5B %02x no admitida", ext);
        return nullptr;
    }
    }
    fail(f, "operación %02x no admitida", op);
    return nullptr;
}

// ------------------------------------------------------------------ objetos con nombre
bool field_list(Frame& f, const uint8_t*& p, const uint8_t* end, Object& tmpl) {
    uint64_t bit = 0;
    uint8_t flags = *p++;
    uint8_t access = flags & 0x0F;
    while (p < end && !f.err) {
        uint8_t c = *p;
        if (c == 0x00) {   // ReservedField
            p++;
            bit += pkg_length(p);
        } else if (c == 0x01) {   // AccessField
            p++;
            access = *p & 0x0F;
            p += 2;
        } else if (c == 0x03) {   // ExtendedAccessField
            p++;
            access = *p & 0x0F;
            p += 3;
        } else if (c == 0x02) {   // ConnectField (GPIO/serie): se salta
            p++;
            if (*p == 0x11) {
                p++;
                const uint8_t* e = pkg_end(f, p);
                p = e;
            } else {
                NamePath np;
                if (!parse_name(f, p, np)) return false;
            }
        } else if (is_lead(c)) {
            uint32_t name = rd32(p);
            p += 4;
            uint32_t len = pkg_length(p);
            Object* fo = new_obj(FIELD);
            fo->field = tmpl.field;
            fo->field.access = access;
            fo->field.lock = (flags >> 4) & 1;
            fo->field.update = (flags >> 5) & 3;
            fo->field.bit_offset = bit;
            fo->field.bit_length = len;
            bit += len;
            Node* n = find_child(f.scope, name);
            if (!n) {
                n = new_node(real(f.scope), name);
                track(f, n);
            }
            if (n) set_obj(n, fo);
            else unref(fo);
        } else {
            fail(f, "elemento de Field no válido (%02x)", c);
            return false;
        }
    }
    return !f.err;
}

Node* named_block(Frame& f, const uint8_t*& p, Type t, int skip, const uint8_t*& end) {
    end = pkg_end(f, p);
    if (f.err) return nullptr;
    NamePath np;
    if (!parse_name(f, p, np)) return nullptr;
    bool existed;
    Node* n = create(f, np, &existed);
    if (!n) return nullptr;
    n = real(n);
    if (!n->obj || n->obj->type == SCOPE || n->obj->type == UNINIT) {
        Object* o = new_obj(t);
        if (t == PROCESSOR) {
            o->event.count = 0;
        }
        set_obj(n, o);
    }
    p += skip;
    return n;
}

// Ejecuta el bloque de un Scope/Device/... con el ámbito cambiado. Al cargar una tabla, un
// error dentro de un bloque no impide cargar el resto.
Flow scoped_block(Frame& f, Node* n, const uint8_t* p, const uint8_t* end) {
    Node* saved = f.scope;
    f.scope = n;
    Flow fl = block(f, p, end);
    f.scope = saved;
    if (fl == F_ERROR && !f.method) {
        f.err = false;
        return F_NEXT;
    }
    return fl;
}

Flow statement(Frame& f, const uint8_t*& p) {
    if (f.err) return F_ERROR;
    if (p >= f.limit) { fail(f, "fin inesperado del código"); return F_ERROR; }
    uint8_t op = *p;
    switch (op) {
    case 0xA0: {   // If / Else
        p++;
        const uint8_t* end = pkg_end(f, p);
        if (f.err) return F_ERROR;
        uint64_t c;
        if (!int_arg(f, p, c)) return F_ERROR;
        const uint8_t* body = p;
        p = end;
        const uint8_t* else_body = nullptr;
        const uint8_t* else_end = nullptr;
        if (p < f.limit && *p == 0xA1) {
            p++;
            else_end = pkg_end(f, p);
            if (f.err) return F_ERROR;
            else_body = p;
            p = else_end;
        }
        if (c) return block(f, body, end);
        if (else_body) return block(f, else_body, else_end);
        return F_NEXT;
    }
    case 0xA1: {   // Else suelto: se salta
        p++;
        p = pkg_end(f, p);
        return f.err ? F_ERROR : F_NEXT;
    }
    case 0xA2: {   // While
        p++;
        const uint8_t* end = pkg_end(f, p);
        if (f.err) return F_ERROR;
        const uint8_t* pred = p;
        uint64_t t0 = H.timer_100ns ? H.timer_100ns() : 0;
        for (uint32_t it = 0;; it++) {
            if (it >= MAX_LOOPS || (H.timer_100ns && (it & 15) == 15 && H.timer_100ns() - t0 > MAX_LOOP_TIME)) {
                fail(f, "bucle While sin fin");
                return F_ERROR;
            }
            const uint8_t* q = pred;
            uint64_t c;
            if (!int_arg(f, q, c)) return F_ERROR;
            if (!c) break;
            Flow fl = block(f, q, end);
            if (fl == F_BREAK) break;
            if (fl == F_CONTINUE || fl == F_NEXT) continue;
            p = end;
            return fl;
        }
        p = end;
        return F_NEXT;
    }
    case 0xA3: p++; return F_NEXT;           // Noop
    case 0xCC: p++; return F_NEXT;           // BreakPoint
    case 0xA5: p++; return F_BREAK;
    case 0x9F: p++; return F_CONTINUE;
    case 0xA4: {   // Return
        p++;
        Object* v = eval(f, p);
        if (!v) return F_ERROR;
        unref(f.ret);
        f.ret = v;
        return F_RETURN;
    }
    case 0x08: {   // Name
        p++;
        NamePath np;
        if (!parse_name(f, p, np)) return F_ERROR;
        Object* v = pkg_element(f, p);
        if (!v) return F_ERROR;
        bool existed;
        Node* n = create(f, np, &existed);
        if (!n) { unref(v); return F_ERROR; }
        if (existed && n->obj) unref(v);   // definición repetida: se queda la primera
        else set_obj(n, v);
        return F_NEXT;
    }
    case 0x10: {   // Scope
        p++;
        const uint8_t* end = pkg_end(f, p);
        if (f.err) return F_ERROR;
        NamePath np;
        if (!parse_name(f, p, np)) return F_ERROR;
        Node* n = real(resolve(f.scope, np));
        if (!n) {
            bool existed;
            n = create(f, np, &existed);
            if (!n) { if (!f.method) { f.err = false; p = end; return F_NEXT; } return F_ERROR; }
            set_obj(n, new_obj(SCOPE));
        }
        Flow fl = scoped_block(f, n, p, end);
        p = end;
        return fl;
    }
    case 0x14: {   // Method
        p++;
        const uint8_t* end = pkg_end(f, p);
        if (f.err) return F_ERROR;
        NamePath np;
        if (!parse_name(f, p, np)) return F_ERROR;
        uint8_t flags = *p++;
        bool existed;
        Node* n = create(f, np, &existed);
        if (n && !(existed && n->obj && n->obj->type == METHOD)) {
            Object* m = new_obj(METHOD);
            m->method.code = p;
            m->method.len = (uint32_t)(end - p);
            m->method.flags = flags;
            set_obj(n, m);
            g_methods++;
        }
        p = end;
        if (!n && !f.method) { f.err = false; return F_NEXT; }
        return n ? F_NEXT : F_ERROR;
    }
    case 0x15: {   // External
        p++;
        NamePath np;
        if (!parse_name(f, p, np)) return F_ERROR;
        p += 2;
        return F_NEXT;
    }
    case 0x06: {   // Alias
        p++;
        NamePath src, al;
        if (!parse_name(f, p, src) || !parse_name(f, p, al)) return F_ERROR;
        Node* s = resolve(f.scope, src);
        bool existed;
        Node* n = create(f, al, &existed);
        if (n && s) n->alias = s;
        return F_NEXT;
    }
    case 0x86: {   // Notify
        p++;
        Target t;
        if (!parse_target(f, p, t)) return F_ERROR;
        uint64_t v;
        if (!int_arg(f, p, v)) { release_target(t); return F_ERROR; }
        if (t.kind == Target::NODE && H.notify) H.notify(t.node, v);
        release_target(t);
        return F_NEXT;
    }
    case 0x5B: {
        if (p + 1 >= f.limit) { fail(f, "fin inesperado del código"); return F_ERROR; }
        uint8_t ext = p[1];
        switch (ext) {
        case 0x01: case 0x02: {   // Mutex / Event
            p += 2;
            NamePath np;
            if (!parse_name(f, p, np)) return F_ERROR;
            if (ext == 0x01) p++;
            bool existed;
            Node* n = create(f, np, &existed);
            if (n && !existed) set_obj(n, new_obj(ext == 0x01 ? MUTEX : EVENT));
            return n ? F_NEXT : F_ERROR;
        }
        case 0x80: {   // OperationRegion
            p += 2;
            NamePath np;
            if (!parse_name(f, p, np)) return F_ERROR;
            uint8_t space = *p++;
            uint64_t off, len;
            if (!int_arg(f, p, off) || !int_arg(f, p, len)) return F_ERROR;
            bool existed;
            Node* n = create(f, np, &existed);
            if (!n) return F_ERROR;
            Object* r = new_obj(REGION);
            r->region.space = space;
            r->region.base = off;
            r->region.length = len;
            r->region.node = n;
            set_obj(n, r);
            return F_NEXT;
        }
        case 0x81: case 0x86: case 0x87: {   // Field / IndexField / BankField
            p += 2;
            const uint8_t* end = pkg_end(f, p);
            if (f.err) return F_ERROR;
            Object tmpl;
            memset(&tmpl, 0, sizeof tmpl);
            NamePath a, b;
            if (!parse_name(f, p, a)) return F_ERROR;
            Node* na = resolve(f.scope, a);
            Node* nb = nullptr;
            if (ext != 0x81) {
                if (!parse_name(f, p, b)) return F_ERROR;
                nb = resolve(f.scope, b);
            }
            if (!na || (ext != 0x81 && !nb)) {
                char t[64];
                name_text(a, t, sizeof t);
                fail(f, "Field: %s no existe", t);
                p = end;
                if (!f.method) { f.err = false; return F_NEXT; }
                return F_ERROR;
            }
            if (ext == 0x81) {
                tmpl.field.kind = 0;
                tmpl.field.region = na;
            } else if (ext == 0x86) {
                tmpl.field.kind = 1;
                tmpl.field.index = na;
                tmpl.field.data = nb;
            } else {
                tmpl.field.kind = 2;
                tmpl.field.region = na;
                tmpl.field.bank = nb;
                uint64_t bv;
                if (!int_arg(f, p, bv)) return F_ERROR;
                tmpl.field.bank_value = bv;
            }
            bool ok = field_list(f, p, end, tmpl);
            p = end;
            if (!ok && !f.method) { f.err = false; return F_NEXT; }
            return ok ? F_NEXT : F_ERROR;
        }
        case 0x82: case 0x83: case 0x84: case 0x85: {   // Device / Processor / PowerResource / ThermalZone
            p += 2;
            const uint8_t* end;
            Type t = ext == 0x82 ? DEVICE : ext == 0x83 ? PROCESSOR : ext == 0x84 ? POWER : THERMAL;
            int skip = ext == 0x83 ? 6 : ext == 0x84 ? 3 : 0;
            Node* n = named_block(f, p, t, skip, end);
            if (!n) {
                if (!f.method && end > p) { f.err = false; p = end; return F_NEXT; }
                return F_ERROR;
            }
            Flow fl = scoped_block(f, n, p, end);
            p = end;
            return fl;
        }
        case 0x88: {   // DataRegion: no se admite (región vacía)
            p += 2;
            NamePath np;
            if (!parse_name(f, p, np)) return F_ERROR;
            for (int i = 0; i < 3; i++) {
                Object* o = eval(f, p);
                if (!o) return F_ERROR;
                unref(o);
            }
            bool existed;
            Node* n = create(f, np, &existed);
            if (n) {
                Object* r = new_obj(REGION);
                r->region.space = 0x80;
                r->region.node = n;
                set_obj(n, r);
            }
            return F_NEXT;
        }
        case 0x20: {   // Load: no se admite
            p += 2;
            NamePath np;
            if (!parse_name(f, p, np)) return F_ERROR;
            Target t;
            if (!parse_target(f, p, t)) return F_ERROR;
            Object* z = make_int(0);
            store(f, t, z);
            unref(z);
            release_target(t);
            return F_NEXT;
        }
        case 0x2A: case 0x24: case 0x26: case 0x27: {   // Unload / Signal / Reset / Release
            p += 2;
            Target t;
            if (!parse_target(f, p, t)) return F_ERROR;
            if (t.kind == Target::NODE && t.node->obj && t.node->obj->type == EVENT) {
                if (ext == 0x24) t.node->obj->event.count++;
                if (ext == 0x26) t.node->obj->event.count = 0;
            }
            release_target(t);
            return F_NEXT;
        }
        case 0x21: case 0x22: {   // Stall / Sleep
            p += 2;
            uint64_t v;
            if (!int_arg(f, p, v)) return F_ERROR;
            if (ext == 0x21) { if (H.stall_us) H.stall_us((uint32_t)(v > 1000 ? 1000 : v)); }
            else if (H.sleep_ms) H.sleep_ms((uint32_t)(v > 2000 ? 2000 : v));
            return F_NEXT;
        }
        case 0x32: {   // Fatal
            p += 2;
            uint8_t type = *p++;
            uint32_t code = rd32(p);
            p += 4;
            uint64_t arg;
            if (!int_arg(f, p, arg)) return F_ERROR;
            aml_log("AML: Fatal tipo %u, código %x, argumento %llx\n", type, code, (unsigned long long)arg);
            return F_NEXT;
        }
        }
        break;
    }
    }
    Object* v = eval(f, p);
    if (!v) return f.err ? F_ERROR : F_NEXT;
    unref(v);
    return F_NEXT;
}

Flow block(Frame& f, const uint8_t* p, const uint8_t* end) {
    const uint8_t* saved = f.limit;
    if (end < f.limit) f.limit = end;
    Flow fl = F_NEXT;
    while (p < end) {
        fl = statement(f, p);
        if (fl != F_NEXT) break;
    }
    f.limit = saved;
    return fl;
}

Frame* new_frame(Node* scope, Node* method, int depth, const uint8_t* limit) {
    auto* f = (Frame*)aml_alloc(sizeof(Frame));
    f->scope = scope;
    f->method = method;
    f->depth = depth;
    f->limit = limit;
    return f;
}

void free_frame(Frame* f) {
    for (auto& a : f->args) unref(a);
    for (auto& l : f->locals) unref(l);
    unref(f->ret);
    // nombres creados por el método: se borran en orden inverso
    for (int i = f->ncreated - 1; i >= 0; i--) remove_node(f->created[i]);
    aml_free(f->created);
    aml_free(f);
}

Object* invoke(Frame& caller, Node* mn, Object** args, int argc) {
    Object* m = mn->obj;
    if (m->method.native) return m->method.native(args, argc);
    if (caller.depth >= MAX_DEPTH) { fail(caller, "demasiadas llamadas anidadas"); return nullptr; }
    Frame* f = new_frame(mn, mn, caller.depth + 1, m->method.code + m->method.len);
    for (int i = 0; i < argc && i < 7; i++) {
        f->args[i] = args[i];
        ref(args[i]);
    }
    Flow fl = block(*f, m->method.code, m->method.code + m->method.len);
    Object* r = nullptr;
    bool err = f->err || fl == F_ERROR;
    if (!err) {
        r = f->ret;
        f->ret = nullptr;
        // el resultado no debe apuntar a variables del método que se acaba
        if (r && r->type == REFERENCE && r->reference.kind == REF_SLOT) {
            Object* v = *r->reference.slot;
            if (v) ref(v);
            unref(r);
            r = v;
        }
        if (!r) r = make_int(0);
    }
    free_frame(f);
    if (err) {
        caller.err = true;   // el error ya se ha registrado
        return nullptr;
    }
    return r;
}

Object* native_osi(Object** a, int n) {
    if (n < 1 || !a[0] || a[0]->type != STRING) return make_int(0);
    bool ok = H.osi ? H.osi(a[0]->string.ptr) : false;
    return make_int(ok ? g_ones : 0);
}

Node* predefined(const char* name, Object* o) {
    uint32_t s = (uint32_t)name[0] | ((uint32_t)name[1] << 8) | ((uint32_t)name[2] << 16) | ((uint32_t)name[3] << 24);
    Node* n = new_node(g_root, s);
    n->obj = o;
    return n;
}

// Ruta en texto ("\\_SB.PCI0", "^^LPC.EC0", "_STA") a NamePath
struct TextPath {
    uint8_t segs[4 * 16];
    NamePath np;
};
bool text_path(const char* s, TextPath& tp) {
    tp.np.root = false;
    tp.np.up = 0;
    if (*s == '\\') { tp.np.root = true; s++; }
    while (*s == '^') { tp.np.up++; s++; }
    int n = 0;
    while (*s && n < 16) {
        int k = 0;
        while (*s && *s != '.' && k < 4) tp.segs[n * 4 + k++] = (uint8_t)*s++;
        while (k < 4) tp.segs[n * 4 + k++] = '_';
        while (*s && *s != '.') s++;
        if (*s == '.') s++;
        n++;
    }
    tp.np.nsegs = n;
    tp.np.segs = tp.segs;
    return true;
}
}   // namespace

// ------------------------------------------------------------------ interfaz pública
Object* make_int(uint64_t v) {
    Object* o = new_obj(INTEGER);
    o->integer = v;
    return o;
}
Object* make_string(const char* s) { return make_str_n(s, (uint32_t)strlen(s)); }
void ref(Object* o) {
    if (o) o->refs++;
}
void unref(Object* o) {
    if (!o || --o->refs) return;
    switch (o->type) {
    case STRING: aml_free(o->string.ptr); break;
    case BUFFER: aml_free(o->buffer.ptr); break;
    case PACKAGE:
        for (uint32_t i = 0; i < o->package.count; i++) unref(o->package.items[i]);
        aml_free(o->package.items);
        break;
    case BUFFER_FIELD: unref(o->buffer_field.buffer); break;
    case REFERENCE: unref(o->reference.container); break;
    case DEBUG_OBJ: return;
    default: break;
    }
    aml_free(o);
}

bool init(const Host& h) {
    H = h;
    if (g_root) return true;
    g_root = (Node*)aml_alloc(sizeof(Node));
    g_root->name = '\\';
    g_root->obj = new_obj(SCOPE);
    static const char* const scopes[] = {"_GPE", "_PR_", "_SB_", "_SI_", "_TZ_"};
    for (const char* s : scopes) predefined(s, new_obj(SCOPE));
    Object* osi = new_obj(METHOD);
    osi->method.flags = 1;
    osi->method.native = native_osi;
    predefined("_OSI", osi);
    predefined("_OS_", make_string("Microsoft Windows NT"));
    predefined("_REV", make_int(2));
    predefined("_GL_", new_obj(MUTEX));
    return true;
}

bool load_table(const uint8_t* t, uint32_t len) {
    if (!g_root || len < 36) return false;
    if (!g_width_set && !memcmp(t, "DSDT", 4)) {
        g_width_set = true;
        if (t[8] < 2) {
            g_int_bits = 32;
            g_ones = 0xFFFFFFFFull;
        }
    }
    Frame* f = new_frame(g_root, nullptr, 0, t + len);
    Flow fl = block(*f, t + 36, t + len);
    bool ok = fl != F_ERROR && !f->err;
    free_frame(f);
    g_tables++;
    return ok;
}

Node* lookup(const char* path, Node* scope) {
    if (!g_root || !path) return nullptr;
    TextPath tp;
    text_path(path, tp);
    Node* base = scope ? scope : g_root;
    if (!tp.np.root && !tp.np.up) {
        // relativa: hijos directos, sin las reglas de búsqueda hacia arriba del AML
        // (un dispositivo sin _HID no debe tomar el de su padre)
        Node* n = base;
        for (int i = 0; i < tp.np.nsegs && n; i++) n = find_child(n, rd32(tp.np.segs + i * 4));
        return n;
    }
    return resolve(base, tp.np);
}

Object* evaluate(Node* scope, const char* path, Object** args, int argc) {
    Node* n = path ? lookup(path, scope) : scope;
    n = real(n);
    if (!n) return nullptr;
    Frame* f = new_frame(n->parent ? n->parent : g_root, nullptr, 0, nullptr);
    Object* r = nullptr;
    if (n->obj && n->obj->type == METHOD) r = invoke(*f, n, args, argc);
    else if (n->obj) r = node_value(*f, n);
    free_frame(f);
    return r;
}

bool eval_int(Node* scope, const char* path, uint64_t& out) {
    Object* o = evaluate(scope, path);
    if (!o) return false;
    bool ok = o->type == INTEGER;
    if (ok) out = o->integer;
    unref(o);
    return ok;
}

bool exists(Node* scope, const char* path) { return lookup(path, scope) != nullptr; }

bool pkg_int(Object* pkg, uint32_t i, uint64_t& out) {
    if (!pkg || pkg->type != PACKAGE || i >= pkg->package.count) return false;
    Object* o = pkg->package.items[i];
    if (!o || o->type != INTEGER) return false;
    out = o->integer;
    return true;
}

const char* pkg_string(Object* pkg, uint32_t i) {
    if (!pkg || pkg->type != PACKAGE || i >= pkg->package.count) return "";
    Object* o = pkg->package.items[i];
    if (o && o->type == STRING) return o->string.ptr;
    return "";
}

Node* root() { return g_root; }
Node* parent(Node* n) { return n ? n->parent : nullptr; }
Object* node_object(Node* n) { n = real(n); return n ? n->obj : nullptr; }
uint32_t node_name(Node* n) { return n ? n->name : 0; }
void node_path(Node* n, char* buf, size_t cap) { path_of(n, buf, cap); }

void walk(Node* from, bool (*cb)(Node*, void*), void* ctx) {
    for (Node* c = from ? from->child : nullptr; c; c = c->next)
        if (cb(c, ctx)) walk(c, cb, ctx);
}

bool device_is(Node* dev, const char* id) {
    char t[16];
    Object* h = evaluate(dev, "_HID");
    bool m = h && eisa_or_hid(h, t) && !strcmp(t, id);
    unref(h);
    if (m) return true;
    Object* c = evaluate(dev, "_CID");
    if (c) {
        if (c->type == PACKAGE) {
            for (uint32_t i = 0; i < c->package.count && !m; i++)
                m = eisa_or_hid(c->package.items[i], t) && !strcmp(t, id);
        } else m = eisa_or_hid(c, t) && !strcmp(t, id);
        unref(c);
    }
    return m;
}

void run_reg(uint8_t space) {
    // cada dispositivo una sola vez, aunque tenga varias regiones de ese espacio
    struct Ctx { uint8_t space; Node* done[64]; int n; } ctx{space, {}, 0};
    walk(g_root, [](Node* n, void* c) {
        auto* x = (Ctx*)c;
        Object* o = n->obj;
        if (o && o->type == REGION && o->region.space == x->space && n->parent) {
            Node* dev = n->parent;
            for (int i = 0; i < x->n; i++)
                if (x->done[i] == dev) return true;
            if (x->n < 64) x->done[x->n++] = dev;
            if (Node* reg = find_child(dev, 0x4745525F /* _REG */)) {
                Object* a[2] = {make_int(x->space), make_int(1)};
                Object* r = evaluate(reg, nullptr, a, 2);
                unref(r);
                unref(a[0]);
                unref(a[1]);
            }
        }
        return true;
    }, &ctx);
}

namespace {
void init_children(Node* n) {
    for (Node* c = n->child; c; c = c->next) {
        Object* o = real(c)->obj;
        if (!o || (o->type != DEVICE && o->type != PROCESSOR && o->type != THERMAL && o->type != SCOPE)) continue;
        uint64_t sta = 0x0F;
        if (o->type != SCOPE && find_child(c, 0x4154535F /* _STA */)) {
            if (!eval_int(c, "_STA", sta)) sta = 0x0F;
        }
        if ((sta & 1) && o->type != SCOPE && find_child(c, 0x494E495F /* _INI */)) {
            Object* r = evaluate(c, "_INI");
            unref(r);
        }
        if ((sta & 1) || (sta & 8)) init_children(c);
    }
}
}

void init_devices() {
    if (Node* sb = lookup("\\_SB_")) {
        if (find_child(sb, 0x494E495F)) unref(evaluate(sb, "_INI"));
    }
    init_children(g_root);
}

Stats stats() { return Stats{g_nodes, g_methods, g_tables, g_errors, g_int_bits}; }
const char* last_error() { return g_last; }

}

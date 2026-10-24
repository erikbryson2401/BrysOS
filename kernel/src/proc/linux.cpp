// ABI de Linux x86-64: llamadas al sistema de los programas de Linux (musl) sobre BrysOS.
// Lo mismo que hace WSL1 en Windows o el "Linuxulator" de FreeBSD: los números, estructuras y
// códigos de error son los de Linux, y por debajo están el VFS, la memoria y los hilos de
// BrysOS. Rutas: "/c/..." es C:\..., "/tmp" es C:\Temp y lo demás cuelga de C:\BrysOS\Linux
// (por ejemplo, /usr/share/x es C:\BrysOS\Linux\usr\share\x); /dev/null, /dev/urandom y
// /etc/resolv.conf son virtuales.
#include "proc/linux.h"
#include "proc/process.h"
#include "proc/sched.h"
#include "arch/io.h"
#include "fs/vfs.h"
#include "drivers/rtc.h"
#include "lib/log.h"
#include "lib/string.h"
#include "mm/memory.h"
#include "net/stack.h"
#include "net/diag.h"
#include "proc/syscall.h"
#include "lib/random.h"
#include "kernel.h"

extern "C" void linux_syscall_entry();

namespace linux_abi {

// --- Números de error de Linux (se devuelven en negativo)
enum {
    EPERM = 1, ENOENT = 2, EINTR = 4, EIO = 5, EBADF = 9, ECHILD = 10, EAGAIN = 11, ENOMEM = 12, EACCES = 13,
    EFAULT = 14, EBUSY = 16, EEXIST = 17, EXDEV = 18, ENOTDIR = 20, EISDIR = 21, EINVAL = 22, EMFILE = 24,
    ENOTTY = 25, ENOSPC = 28, ESPIPE = 29, EROFS = 30, EPIPE = 32, ERANGE = 34, ENAMETOOLONG = 36, ENOSYS = 38,
    ENOTEMPTY = 39, ELOOP = 40, ENOTSOCK = 88, EOPNOTSUPP = 95,
};

// --- Descriptores de archivo
struct Fd {
    enum Type { CONSOLE_IN, CONSOLE_OUT, FILE, DIR, NUL, ZERO, RANDOM, MEMFILE, SOCKET, EVENTFD, PIPE_R, PIPE_W } type;
    String path;                // ruta de BrysOS (FILE, DIR)
    uint8_t* data = nullptr;    // contenido completo (FILE, MEMFILE)
    uint64_t size = 0, cap = 0, pos = 0;
    bool readable = true, writable = false, append = false, dirty = false;
    uint32_t flags = 0;         // O_NONBLOCK...
    Vec<VfsEntry> entries;      // DIR
    size_t dir_next = 0;
    int refs = 1;
    void* sock = nullptr;       // SOCKET (net/socket.c)
    uint64_t counter = 0;       // EVENTFD
    bool semaphore = false;
    struct Pipe* pipe = nullptr;   // PIPE_R, PIPE_W
};

// Tubería: búfer circular compartido por el extremo de lectura y el de escritura
struct Pipe {
    static constexpr uint64_t CAP = 65536;
    uint8_t buf[CAP];
    uint64_t head = 0, len = 0;
    int readers = 1, writers = 1;
};

struct Map {
    uint64_t addr, len;
};

struct State {
    Vec<Fd*> fds;               // índice = descriptor; nullptr = libre
    Vec<bool> cloexec;
    String cwd;                 // carpeta actual, en forma de Linux
    uint64_t mmap_next = 0x10000000000ull;   // 1 TiB
    Vec<Map> maps;
    String out_line;            // salida estándar a medio escribir (va al registro por líneas)
};

int64_t brys_linux_socket(State* s, int domain, int type, int proto);
int64_t brys_linux_socketcall(uint64_t n, uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t g);

namespace {
constexpr int MAX_FDS = 256;
constexpr uint64_t USER_TOP = 0x0000800000000000ull;
constexpr uint64_t PAGE = 4096;

// O_* de Linux
constexpr uint32_t O_ACCMODE = 3, O_WRONLY = 1, O_RDWR = 2, O_CREAT = 0100, O_EXCL = 0200, O_TRUNC = 01000,
                   O_APPEND = 02000, O_NONBLOCK = 04000, O_DIRECTORY = 0200000, O_CLOEXEC = 02000000;
constexpr int AT_FDCWD = -100;
constexpr uint32_t AT_REMOVEDIR = 0x200;

int64_t err(int e) { return -(int64_t)e; }

State* st() { return process::current()->lx; }


// --- Tiempo: segundos desde 1970 (UTC)
int64_t days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}
int64_t unix_of(const DateTime& t) {
    return days_from_civil(t.year, t.month, t.day) * 86400 + t.hour * 3600 + t.minute * 60 + t.second;
}
int64_t now_utc() { return unix_of(rtc::now()) - (int64_t)rtc::utc_offset() * 60; }
uint64_t mono_ns() { return pit::ticks() * 1000000ull; }

// --- Memoria del programa
bool uok(uint64_t addr, uint64_t len, bool write) { return process::user_ok(process::current(), addr, len, write); }
bool ustr(uint64_t addr, String& s) { return process::user_string(process::current(), addr, s, 4096); }

// --- Descriptores
int alloc_fd(State* s, Fd* f, int from = 0, bool cloexec = false) {
    for (int i = from; i < MAX_FDS; i++) {
        if (i >= (int)s->fds.size()) { s->fds.push(nullptr); s->cloexec.push(false); }
        if (!s->fds[(size_t)i]) {
            s->fds[(size_t)i] = f;
            s->cloexec[(size_t)i] = cloexec;
            return i;
        }
    }
    return -1;
}

Fd* get_fd(State* s, int64_t fd) {
    if (fd < 0 || fd >= (int64_t)s->fds.size()) return nullptr;
    return s->fds[(size_t)fd];
}

void flush(Fd* f) {
    if (f->type == Fd::FILE && f->dirty) {
        if (!vfs::write_file(f->path.c_str(), f->data, (uint32_t)f->size))
            klog("Linux: no se ha podido guardar %s: %s\n", f->path.c_str(), vfs::error());
        f->dirty = false;
    }
}

void release(Fd* f) {
    if (--f->refs > 0) return;
    flush(f);
    if (f->type == Fd::SOCKET && f->sock) brys_socket_close(f->sock);
    if (f->pipe) {
        if (f->type == Fd::PIPE_R) f->pipe->readers--;
        else f->pipe->writers--;
        if (!f->pipe->readers && !f->pipe->writers) delete f->pipe;
    }
    mm::kfree(f->data);
    delete f;
}

int64_t close_fd(State* s, int64_t fd) {
    Fd* f = get_fd(s, fd);
    if (!f) return err(EBADF);
    s->fds[(size_t)fd] = nullptr;
    release(f);
    return 0;
}

bool reserve(Fd* f, uint64_t need) {
    if (need <= f->cap) return true;
    uint64_t cap = f->cap ? f->cap : 4096;
    while (cap < need) cap *= 2;
    auto* d = (uint8_t*)mm::krealloc(f->data, cap);
    if (!d) return false;
    f->data = d;
    f->cap = cap;
    return true;
}

// --- Rutas
void normalize_linux(const char* path, const char* cwd, String& out) {
    String full = path[0] == '/' ? String(path) : String(cwd) + "/" + path;
    Vec<String> parts;
    const char* p = full.c_str();
    while (*p) {
        while (*p == '/') p++;
        const char* e = p;
        while (*e && *e != '/') e++;
        String c(p, (size_t)(e - p));
        if (c == "..") { if (!parts.empty()) parts.pop(); }
        else if (!c.empty() && !(c == ".")) parts.push(c);
        p = e;
    }
    out = "";
    for (auto& c : parts) { out += "/"; out += c; }
    if (out.empty()) out = "/";
}

// Archivos virtuales
enum Special { NONE, DEV_NULL, DEV_ZERO, DEV_RANDOM, DEV_TTY, ETC_RESOLV, ETC_HOSTS, ROOT };
Special special(const String& lp) {
    if (lp == "/dev/null") return DEV_NULL;
    if (lp == "/dev/zero") return DEV_ZERO;
    if (lp == "/dev/urandom" || lp == "/dev/random") return DEV_RANDOM;
    if (lp == "/dev/tty" || lp == "/dev/stdout" || lp == "/dev/stderr") return DEV_TTY;
    if (lp == "/etc/resolv.conf") return ETC_RESOLV;
    if (lp == "/etc/hosts") return ETC_HOSTS;
    if (lp == "/") return ROOT;
    return NONE;
}

String resolv_conf() {
    // un programa suele leerlo antes de abrir su primer socket: la red arranca aquí si hace falta
    net::wait_for_ip(10000);
    String r = "# BrysOS: servidores DNS de la red\n";
    char buf[64];
    for (int i = 0; i < 2; i++)
        if (brys_stack_dns_server(i, buf, sizeof buf)) r += String::format("nameserver %s\n", buf);
    return r;
}

const char* HOSTS = "127.0.0.1 localhost brysos\n";

Fd* mem_file(const String& content) {
    auto* f = new Fd();
    f->type = Fd::MEMFILE;
    f->size = content.size();
    f->data = (uint8_t*)mm::kmalloc(f->size + 1);
    if (f->data) memcpy(f->data, content.c_str(), f->size);
    return f;
}

// Ruta de Linux (relativa a la carpeta actual) -> ruta de BrysOS
bool brys_path(uint64_t upath, String& lp, String& bp, Special& sp) {
    String raw;
    if (!ustr(upath, raw)) return false;
    normalize_linux(raw.c_str(), st()->cwd.c_str(), lp);
    sp = special(lp);
    char out[512];
    if (!to_brys_path(lp.c_str(), "/", out, sizeof out)) return false;
    bp = out;
    return true;
}

// --- stat de Linux (struct stat de x86-64, 144 bytes)
struct [[gnu::packed]] LinuxStat {
    uint64_t dev, ino, nlink;
    uint32_t mode, uid, gid, pad0;
    uint64_t rdev;
    int64_t size, blksize, blocks;
    int64_t atime, atime_ns, mtime, mtime_ns, ctime, ctime_ns;
    int64_t reserved[3];
};
static_assert(sizeof(LinuxStat) == 144, "struct stat de Linux");

constexpr uint32_t S_IFDIR = 0040000, S_IFREG = 0100000, S_IFCHR = 0020000;

void fill_stat(LinuxStat& s, uint32_t mode, uint64_t size, const DateTime* mt, uint64_t ino) {
    memset(&s, 0, sizeof s);
    s.dev = 1;
    s.ino = ino;
    s.nlink = 1;
    s.mode = mode;
    s.size = (int64_t)size;
    s.blksize = 4096;
    s.blocks = (int64_t)((size + 511) / 512);
    int64_t t = mt && mt->year ? unix_of(*mt) - (int64_t)rtc::utc_offset() * 60 : now_utc();
    s.atime = s.mtime = s.ctime = t;
}

uint64_t hash_ino(const String& p) {
    uint64_t h = 1469598103934665603ull;
    for (const char* c = p.c_str(); *c; c++) h = (h ^ (uint8_t)to_upper(*c)) * 1099511628211ull;
    return h | 1;
}

int64_t stat_path(const String& lp, const String& bp, Special sp, LinuxStat& s) {
    switch (sp) {
    case DEV_NULL: case DEV_ZERO: case DEV_RANDOM: case DEV_TTY:
        fill_stat(s, S_IFCHR | 0666, 0, nullptr, hash_ino(lp));
        return 0;
    case ETC_RESOLV: fill_stat(s, S_IFREG | 0644, resolv_conf().size(), nullptr, hash_ino(lp)); return 0;
    case ETC_HOSTS: fill_stat(s, S_IFREG | 0644, strlen(HOSTS), nullptr, hash_ino(lp)); return 0;
    case ROOT: fill_stat(s, S_IFDIR | 0755, 0, nullptr, 2); return 0;
    default: break;
    }
    if (bp.size() == 3 && bp[1] == ':') {   // raíz de una unidad
        if (!vfs::volume(to_upper(bp[0]))) return err(ENOENT);
        fill_stat(s, S_IFDIR | 0755, 0, nullptr, hash_ino(bp));
        return 0;
    }
    VfsEntry e;
    if (!vfs::stat(bp.c_str(), e)) return err(ENOENT);
    fill_stat(s, e.dir ? (S_IFDIR | 0755) : (S_IFREG | 0644), e.size, &e.mtime, hash_ino(bp));
    return 0;
}

int64_t stat_fd(Fd* f, LinuxStat& s) {
    switch (f->type) {
    case Fd::CONSOLE_IN: case Fd::CONSOLE_OUT: case Fd::NUL: case Fd::ZERO: case Fd::RANDOM:
        fill_stat(s, S_IFCHR | 0666, 0, nullptr, 3 + f->type);
        return 0;
    case Fd::MEMFILE: fill_stat(s, S_IFREG | 0644, f->size, nullptr, (uint64_t)f); return 0;
    case Fd::SOCKET: fill_stat(s, 0140000 | 0777, 0, nullptr, (uint64_t)f); return 0;
    case Fd::PIPE_R: case Fd::PIPE_W: fill_stat(s, 0010000 | 0600, 0, nullptr, (uint64_t)f->pipe); return 0;
    case Fd::EVENTFD: fill_stat(s, 0600, 0, nullptr, (uint64_t)f); return 0;
    case Fd::DIR: fill_stat(s, S_IFDIR | 0755, 0, nullptr, hash_ino(f->path)); return 0;
    case Fd::FILE: {
        VfsEntry e;
        bool ok = vfs::stat(f->path.c_str(), e);
        fill_stat(s, S_IFREG | 0644, f->size, ok ? &e.mtime : nullptr, hash_ino(f->path));
        return 0;
    }
    }
    return err(EBADF);
}

// --- Abrir
int64_t do_open(uint64_t upath, uint32_t flags) {
    State* s = st();
    String lp, bp;
    Special sp;
    if (!brys_path(upath, lp, bp, sp)) return err(EFAULT);
    Fd* f = nullptr;
    switch (sp) {
    case DEV_NULL: f = new Fd(); f->type = Fd::NUL; break;
    case DEV_ZERO: f = new Fd(); f->type = Fd::ZERO; break;
    case DEV_RANDOM: f = new Fd(); f->type = Fd::RANDOM; break;
    case DEV_TTY: f = new Fd(); f->type = Fd::CONSOLE_OUT; break;
    case ETC_RESOLV: f = mem_file(resolv_conf()); break;
    case ETC_HOSTS: f = mem_file(HOSTS); break;
    case ROOT:
        f = new Fd();
        f->type = Fd::DIR;
        for (Volume* v : vfs::volumes()) {
            VfsEntry e;
            e.name = String::format("%c", to_lower(v->letter));
            e.dir = true;
            f->entries.push(e);
        }
        break;
    default: break;
    }
    if (!f) {
        bool is_drive_root = bp.size() == 3 && bp[1] == ':';
        bool exists = is_drive_root ? vfs::volume(to_upper(bp[0])) != nullptr : vfs::exists(bp.c_str());
        bool is_dir = is_drive_root || (exists && vfs::is_dir(bp.c_str()));
        uint32_t acc = flags & O_ACCMODE;
        if (is_dir) {
            if (acc != 0) return err(EISDIR);
            f = new Fd();
            f->type = Fd::DIR;
            f->path = bp;
            vfs::list(bp.c_str(), f->entries);
        } else {
            if (flags & O_DIRECTORY) return err(exists ? ENOTDIR : ENOENT);
            if (exists && (flags & O_CREAT) && (flags & O_EXCL)) return err(EEXIST);
            if (!exists && !(flags & O_CREAT)) return err(ENOENT);
            if (acc != 0 && vfs::is_system_path(bp.c_str())) return err(EACCES);
            f = new Fd();
            f->type = Fd::FILE;
            f->path = bp;
            f->readable = acc != O_WRONLY;
            f->writable = acc != 0;
            f->append = flags & O_APPEND;
            if (exists && !(flags & O_TRUNC)) {
                uint8_t* d;
                uint32_t n;
                if (!vfs::read_file(bp.c_str(), d, n)) { delete f; return err(EIO); }
                f->data = d;
                f->size = f->cap = n;
            }
            if (!exists || (flags & O_TRUNC)) {
                // se crea (o se vacía) ya, como en Linux
                if (!vfs::write_file(bp.c_str(), "", 0)) { delete f; return err(EACCES); }
            }
        }
    }
    f->flags = flags & O_NONBLOCK;
    int fd = alloc_fd(s, f, 0, flags & O_CLOEXEC);
    if (fd < 0) { release(f); return err(EMFILE); }
    return fd;
}

// --- Leer y escribir
void console_write(State* s, const uint8_t* d, uint64_t n) {
    for (uint64_t i = 0; i < n; i++) {
        if (d[i] == '\n' || s->out_line.size() > 400) {
            klog("[%s] %s\n", process::current()->name.c_str(), s->out_line.c_str());
            s->out_line.clear();
            if (d[i] == '\n') continue;
        }
        if (d[i] != '\r') s->out_line.append((char)d[i]);
    }
}

int64_t do_read(Fd* f, uint8_t* buf, uint64_t n) {
    switch (f->type) {
    case Fd::CONSOLE_IN: return 0;   // fin de archivo: los programas no tienen teclado de consola
    case Fd::NUL: return 0;
    case Fd::ZERO: memset(buf, 0, n); return (int64_t)n;
    case Fd::RANDOM:
        random::bytes(buf, n);
        return (int64_t)n;
    case Fd::FILE: case Fd::MEMFILE: {
        if (!f->readable) return err(EBADF);
        if (f->pos >= f->size) return 0;
        uint64_t k = min(n, f->size - f->pos);
        memcpy(buf, f->data + f->pos, k);
        f->pos += k;
        return (int64_t)k;
    }
    case Fd::SOCKET: return brys_socket_recv(f->sock, buf, n, (f->flags & O_NONBLOCK) != 0);
    case Fd::EVENTFD: {
        if (n < 8) return err(EINVAL);
        while (!f->counter) {
            if (f->flags & O_NONBLOCK) return err(EAGAIN);
            sched::sleep(2);
        }
        uint64_t v = f->semaphore ? 1 : f->counter;
        f->counter -= v;
        memcpy(buf, &v, 8);
        return 8;
    }
    case Fd::PIPE_R: {
        Pipe* pp = f->pipe;
        while (!pp->len) {
            if (!pp->writers) return 0;
            if (f->flags & O_NONBLOCK) return err(EAGAIN);
            sched::sleep(2);
        }
        uint64_t k = min(n, pp->len);
        for (uint64_t i = 0; i < k; i++) buf[i] = pp->buf[(pp->head + i) % Pipe::CAP];
        pp->head = (pp->head + k) % Pipe::CAP;
        pp->len -= k;
        return (int64_t)k;
    }
    case Fd::DIR: return err(EISDIR);
    default: return err(EBADF);
    }
}

int64_t do_write(State* s, Fd* f, const uint8_t* buf, uint64_t n) {
    switch (f->type) {
    case Fd::CONSOLE_OUT: console_write(s, buf, n); return (int64_t)n;
    case Fd::NUL: case Fd::ZERO: case Fd::RANDOM: return (int64_t)n;
    case Fd::FILE: {
        if (!f->writable) return err(EBADF);
        if (f->append) f->pos = f->size;
        if (!reserve(f, f->pos + n)) return err(ENOSPC);
        if (f->pos > f->size) memset(f->data + f->size, 0, f->pos - f->size);
        memcpy(f->data + f->pos, buf, n);
        f->pos += n;
        if (f->pos > f->size) f->size = f->pos;
        f->dirty = true;
        return (int64_t)n;
    }
    case Fd::SOCKET: return brys_socket_send(f->sock, buf, n, (f->flags & O_NONBLOCK) != 0);
    case Fd::EVENTFD: {
        if (n < 8) return err(EINVAL);
        uint64_t v;
        memcpy(&v, buf, 8);
        f->counter += v;
        return 8;
    }
    case Fd::PIPE_W: {
        Pipe* pp = f->pipe;
        uint64_t done = 0;
        while (done < n) {
            if (!pp->readers) return done ? (int64_t)done : err(EPIPE);
            uint64_t room = Pipe::CAP - pp->len;
            if (!room) {
                if (f->flags & O_NONBLOCK) return done ? (int64_t)done : err(EAGAIN);
                sched::sleep(2);
                continue;
            }
            uint64_t k = min(n - done, room);
            for (uint64_t i = 0; i < k; i++) pp->buf[(pp->head + pp->len + i) % Pipe::CAP] = buf[done + i];
            pp->len += k;
            done += k;
        }
        return (int64_t)done;
    }
    default: return err(EBADF);
    }
}

struct IoVec {
    uint64_t base, len;
};

// --- getdents64
int64_t do_getdents(Fd* f, uint64_t buf, uint64_t cap) {
    if (f->type != Fd::DIR) return err(ENOTDIR);
    if (!uok(buf, cap, true)) return err(EFAULT);
    uint64_t used = 0;
    // "." y ".." primero
    while (f->dir_next < f->entries.size() + 2) {
        size_t i = f->dir_next;
        String name = i == 0 ? String(".") : i == 1 ? String("..") : f->entries[i - 2].name;
        bool dir = i < 2 || f->entries[i - 2].dir;
        uint64_t reclen = (19 + name.size() + 1 + 7) & ~7ull;
        if (used + reclen > cap) break;
        uint8_t* d = (uint8_t*)(buf + used);
        memset(d, 0, reclen);
        *(uint64_t*)d = hash_ino(f->path + "\\" + name);
        *(int64_t*)(d + 8) = (int64_t)(i + 1);
        *(uint16_t*)(d + 16) = (uint16_t)reclen;
        d[18] = dir ? 4 : 8;   // DT_DIR / DT_REG
        memcpy(d + 19, name.c_str(), name.size() + 1);
        used += reclen;
        f->dir_next++;
    }
    if (!used && f->dir_next < f->entries.size() + 2) return err(EINVAL);
    return (int64_t)used;
}

// --- mmap
constexpr uint64_t MAP_FIXED = 0x10, MAP_ANONYMOUS = 0x20;

int64_t do_mmap(uint64_t addr, uint64_t len, uint64_t flags, int64_t fd, uint64_t off) {
    State* s = st();
    Process* p = process::current();
    if (!len) return err(EINVAL);
    len = (len + PAGE - 1) & ~(PAGE - 1);
    Fd* f = nullptr;
    if (!(flags & MAP_ANONYMOUS)) {
        f = get_fd(s, fd);
        if (!f || (f->type != Fd::FILE && f->type != Fd::MEMFILE)) return err(EBADF);
    }
    uint64_t at;
    if (flags & MAP_FIXED) {
        if (addr & (PAGE - 1) || addr < RUN_BASE || addr + len > USER_TOP) return err(EINVAL);
        at = addr;
        process::free_range(p, at, len);
    } else {
        at = s->mmap_next;
        s->mmap_next += len + PAGE;   // una página libre entre zonas
    }
    if (!process::alloc_range(p, at, len)) return err(ENOMEM);
    if (f && off < f->size) process::poke(p, at, f->data + off, min(len, f->size - off));
    s->maps.push({at, len});
    return (int64_t)at;
}

int64_t do_munmap(uint64_t addr, uint64_t len) {
    if (addr & (PAGE - 1)) return err(EINVAL);
    len = (len + PAGE - 1) & ~(PAGE - 1);
    process::free_range(process::current(), addr, len);
    return 0;
}

int64_t do_mremap(uint64_t old, uint64_t old_len, uint64_t new_len, uint64_t flags) {
    constexpr uint64_t MREMAP_MAYMOVE = 1;
    old_len = (old_len + PAGE - 1) & ~(PAGE - 1);
    new_len = (new_len + PAGE - 1) & ~(PAGE - 1);
    if (new_len <= old_len) {
        do_munmap(old + new_len, old_len - new_len);
        return (int64_t)old;
    }
    if (!(flags & MREMAP_MAYMOVE)) return err(ENOMEM);
    int64_t n = do_mmap(0, new_len, MAP_ANONYMOUS, -1, 0);
    if (n < 0) return n;
    memcpy((void*)n, (void*)old, old_len);   // el espacio del programa es el activo
    do_munmap(old, old_len);
    return n;
}

int64_t do_brk(uint64_t want) {
    Process* p = process::current();
    if (!want || want < p->brk_base) return (int64_t)p->brk;
    uint64_t top = (want + PAGE - 1) & ~(PAGE - 1);
    if (top > 0x40000000ull) return (int64_t)p->brk;
    if (top > p->brk && !process::alloc_range(p, p->brk, top - p->brk)) return (int64_t)p->brk;
    p->brk = want;
    return (int64_t)want;
}

// --- Varios
struct Utsname {
    char sysname[65], nodename[65], release[65], version[65], machine[65], domainname[65];
};

void put_str(char* d, const char* s) { strlcpy(d, s, 65); }

int64_t do_clock_gettime(uint64_t clk, uint64_t uts) {
    if (!uok(uts, 16, true)) return err(EFAULT);
    int64_t* ts = (int64_t*)uts;
    if (clk == 0 || clk == 5 || clk == 8) {   // REALTIME, REALTIME_COARSE, TAI
        ts[0] = now_utc();
        ts[1] = (int64_t)((pit::ticks() % 1000) * 1000000);
    } else {
        uint64_t ns = mono_ns();
        ts[0] = (int64_t)(ns / 1000000000ull);
        ts[1] = (int64_t)(ns % 1000000000ull);
    }
    return 0;
}

int64_t do_sleep_ns(uint64_t ns) {
    uint64_t ms = (ns + 999999) / 1000000;
    if (ms) sched::sleep((uint32_t)min<uint64_t>(ms, 3600000));
    else sched::yield();
    return 0;
}

// Señales: no hay manejadores; las que matan terminan el programa
void fatal_signal(int sig) {
    Process* p = process::current();
    p->crashed = true;
    p->crash_info = String::format("señal %d", sig);
    klog("Programa: %s (proceso %d) terminado por la señal %d\n", p->name.c_str(), p->pid, sig);
    process::exit_current(128 + sig);
}

bool warned[512];

int64_t sys(uint64_t n, uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t g) {
    State* s = st();
    Process* p = process::current();
    switch (n) {
    case 0: {   // read
        Fd* f = get_fd(s, (int64_t)a);
        if (!f) return err(EBADF);
        if (!uok(b, c, true)) return err(EFAULT);
        return do_read(f, (uint8_t*)b, c);
    }
    case 1: {   // write
        Fd* f = get_fd(s, (int64_t)a);
        if (!f) return err(EBADF);
        if (!uok(b, c, false)) return err(EFAULT);
        return do_write(s, f, (const uint8_t*)b, c);
    }
    case 2: return do_open(a, (uint32_t)b);                              // open
    case 3: return close_fd(s, (int64_t)a);                               // close
    case 4: case 6: {                                                     // stat, lstat
        String lp, bp;
        Special sp;
        if (!brys_path(a, lp, bp, sp) || !uok(b, sizeof(LinuxStat), true)) return err(EFAULT);
        return stat_path(lp, bp, sp, *(LinuxStat*)b);
    }
    case 5: {                                                             // fstat
        Fd* f = get_fd(s, (int64_t)a);
        if (!f) return err(EBADF);
        if (!uok(b, sizeof(LinuxStat), true)) return err(EFAULT);
        return stat_fd(f, *(LinuxStat*)b);
    }
    case 7: case 271: {                                                   // poll, ppoll
        struct PollFd { int32_t fd; int16_t events, revents; };
        if (!uok(a, b * sizeof(PollFd), true)) return err(EFAULT);
        auto* fds = (PollFd*)a;
        int64_t timeout_ms = n == 7 ? (int64_t)(int32_t)c : -1;
        if (n == 271 && c) {
            if (!uok(c, 16, false)) return err(EFAULT);
            const int64_t* ts = (const int64_t*)c;
            timeout_ms = ts[0] * 1000 + ts[1] / 1000000;
        }
        uint64_t end = pit::ticks() + (uint64_t)(timeout_ms < 0 ? 0 : timeout_ms);
        for (;;) {
            int ready = 0;
            for (uint64_t i = 0; i < b; i++) {
                fds[i].revents = 0;
                Fd* f = get_fd(s, fds[i].fd);
                if (!f) { if (fds[i].fd >= 0) { fds[i].revents = 0x20; ready++; } continue; }   // POLLNVAL
                short ev = 0;
                if (f->type == Fd::SOCKET) ev = (short)brys_socket_poll(f->sock);
                else if (f->type == Fd::EVENTFD) ev = (short)((f->counter ? 0x1 : 0) | 0x4);
                else if (f->type == Fd::PIPE_R) ev = (short)((f->pipe->len ? 0x1 : 0) | (f->pipe->writers ? 0 : 0x10));
                else if (f->type == Fd::PIPE_W) ev = (short)((f->pipe->len < Pipe::CAP ? 0x4 : 0) | (f->pipe->readers ? 0 : 0x8));
                else ev = 0x1 | 0x4;   // POLLIN | POLLOUT
                fds[i].revents = (short)(ev & (fds[i].events | 0x8 | 0x10));   // + POLLERR, POLLHUP
                if (fds[i].revents) ready++;
            }
            if (ready || timeout_ms == 0 || (timeout_ms > 0 && pit::ticks() >= end)) return ready;
            sched::sleep(5);
        }
    }
    case 8: {                                                             // lseek
        Fd* f = get_fd(s, (int64_t)a);
        if (!f) return err(EBADF);
        if (f->type != Fd::FILE && f->type != Fd::MEMFILE) return err(ESPIPE);
        int64_t base = c == 0 ? 0 : c == 1 ? (int64_t)f->pos : (int64_t)f->size;
        int64_t np = base + (int64_t)b;
        if (np < 0 || c > 2) return err(EINVAL);
        f->pos = (uint64_t)np;
        return np;
    }
    case 9: return do_mmap(a, b, d, (int64_t)e, g);                       // mmap
    case 10: return 0;                                                    // mprotect
    case 11: return do_munmap(a, b);                                      // munmap
    case 12: return do_brk(a);                                            // brk
    case 13: case 14: case 131: return 0;                                 // rt_sigaction, rt_sigprocmask, sigaltstack
    case 16: {                                                            // ioctl
        Fd* f = get_fd(s, (int64_t)a);
        if (!f) return err(EBADF);
        if (b == 0x5413 && (f->type == Fd::CONSOLE_OUT || f->type == Fd::CONSOLE_IN)) {   // TIOCGWINSZ
            if (!uok(c, 8, true)) return err(EFAULT);
            uint16_t* ws = (uint16_t*)c;
            ws[0] = 25; ws[1] = 80; ws[2] = ws[3] = 0;
            return 0;
        }
        if (b == 0x5421 && f->type == Fd::SOCKET) {   // FIONBIO
            if (!uok(c, 4, false)) return err(EFAULT);
            if (*(int*)c) f->flags |= O_NONBLOCK; else f->flags &= ~O_NONBLOCK;
            return 0;
        }
        if (b == 0x541B) {   // FIONREAD
            if (!uok(c, 4, true)) return err(EFAULT);
            *(int*)c = f->type == Fd::SOCKET ? brys_socket_available(f->sock) :
                       (f->type == Fd::FILE || f->type == Fd::MEMFILE) ? (int)(f->size - min(f->pos, f->size)) : 0;
            return 0;
        }
        return err(ENOTTY);
    }
    case 17: case 18: {                                                   // pread64, pwrite64
        Fd* f = get_fd(s, (int64_t)a);
        if (!f) return err(EBADF);
        if (f->type != Fd::FILE && f->type != Fd::MEMFILE) return err(ESPIPE);
        if (!uok(b, c, n == 17)) return err(EFAULT);
        uint64_t old = f->pos;
        f->pos = d;
        bool app = f->append;
        f->append = false;
        int64_t r = n == 17 ? do_read(f, (uint8_t*)b, c) : do_write(s, f, (const uint8_t*)b, c);
        f->append = app;
        f->pos = old;
        return r;
    }
    case 19: case 20: {                                                   // readv, writev
        Fd* f = get_fd(s, (int64_t)a);
        if (!f) return err(EBADF);
        if (!uok(b, c * sizeof(IoVec), false)) return err(EFAULT);
        const IoVec* iov = (const IoVec*)b;
        int64_t total = 0;
        for (uint64_t i = 0; i < c; i++) {
            if (!iov[i].len) continue;
            if (!uok(iov[i].base, iov[i].len, n == 19)) return total ? total : err(EFAULT);
            int64_t r = n == 19 ? do_read(f, (uint8_t*)iov[i].base, iov[i].len)
                                : do_write(s, f, (const uint8_t*)iov[i].base, iov[i].len);
            if (r < 0) return total ? total : r;
            total += r;
            if ((uint64_t)r < iov[i].len) break;
        }
        return total;
    }
    case 21: case 269: {                                                  // access, faccessat
        uint64_t path = n == 21 ? a : b;
        String lp, bp;
        Special sp;
        if (!brys_path(path, lp, bp, sp)) return err(EFAULT);
        LinuxStat stbuf;
        return stat_path(lp, bp, sp, stbuf) < 0 ? err(ENOENT) : 0;
    }
    case 24: sched::yield(); return 0;                                    // sched_yield
    case 25: return do_mremap(a, b, c, d);                                // mremap
    case 28: return 0;                                                    // madvise
    case 32: {                                                            // dup
        Fd* f = get_fd(s, (int64_t)a);
        if (!f) return err(EBADF);
        f->refs++;
        int fd = alloc_fd(s, f);
        if (fd < 0) { f->refs--; return err(EMFILE); }
        return fd;
    }
    case 33: case 292: {                                                  // dup2, dup3
        Fd* f = get_fd(s, (int64_t)a);
        if (!f || (int64_t)b < 0 || b >= MAX_FDS) return err(EBADF);
        if (a == b) return n == 33 ? (int64_t)b : err(EINVAL);
        while ((int64_t)s->fds.size() <= (int64_t)b) { s->fds.push(nullptr); s->cloexec.push(false); }
        if (s->fds[b]) close_fd(s, (int64_t)b);
        f->refs++;
        s->fds[b] = f;
        s->cloexec[b] = n == 292 && (c & O_CLOEXEC);
        return (int64_t)b;
    }
    case 35: {                                                            // nanosleep
        if (!uok(a, 16, false)) return err(EFAULT);
        const int64_t* ts = (const int64_t*)a;
        return do_sleep_ns((uint64_t)ts[0] * 1000000000ull + (uint64_t)ts[1]);
    }
    case 39: case 186: return p->pid;                                     // getpid, gettid
    case 110: return 1;                                                   // getppid
    case 102: case 104: case 107: case 108: return 0;                     // getuid, getgid, geteuid, getegid
    case 60: case 231:                                                    // exit, exit_group
        destroy_state(p);
        process::exit_current((int)a);
    case 62: case 200: case 234:                                          // kill, tkill, tgkill
        {
            int sig = (int)(n == 234 ? c : b);
            if (sig == 0) return 0;
            if (sig == 6 || sig == 9 || sig == 11 || sig == 15 || sig == 4 || sig == 8 || sig == 7) {
                destroy_state(p);
                fatal_signal(sig);
            }
            return 0;
        }
    case 63: {                                                            // uname
        if (!uok(a, sizeof(Utsname), true)) return err(EFAULT);
        Utsname* u = (Utsname*)a;
        memset(u, 0, sizeof *u);
        put_str(u->sysname, "Linux");
        put_str(u->nodename, "brysos");
        put_str(u->release, "6.12.0-brysos");
        put_str(u->version, "BrysOS " BRYSOS_VERSION);
        put_str(u->machine, "x86_64");
        return 0;
    }
    case 72: {                                                            // fcntl
        Fd* f = get_fd(s, (int64_t)a);
        if (!f) return err(EBADF);
        switch (b) {
        case 0: case 1030: {   // F_DUPFD, F_DUPFD_CLOEXEC
            f->refs++;
            int fd = alloc_fd(s, f, (int)c, b == 1030);
            if (fd < 0) { f->refs--; return err(EMFILE); }
            return fd;
        }
        case 1: return s->cloexec[(size_t)a] ? 1 : 0;   // F_GETFD
        case 2: s->cloexec[(size_t)a] = c & 1; return 0;   // F_SETFD
        case 3: return (f->writable ? (f->readable ? O_RDWR : O_WRONLY) : 0) | f->flags | (f->append ? O_APPEND : 0);
        case 4: f->flags = (uint32_t)(c & O_NONBLOCK); f->append = c & O_APPEND; return 0;   // F_SETFL
        default: return 0;   // cerrojos de archivo: no hacen falta con un solo usuario
        }
    }
    case 74: case 75: {                                                   // fsync, fdatasync
        Fd* f = get_fd(s, (int64_t)a);
        if (!f) return err(EBADF);
        flush(f);
        return 0;
    }
    case 77: {                                                            // ftruncate
        Fd* f = get_fd(s, (int64_t)a);
        if (!f || f->type != Fd::FILE || !f->writable) return err(EBADF);
        if (!reserve(f, b)) return err(ENOSPC);
        if (b > f->size) memset(f->data + f->size, 0, b - f->size);
        f->size = b;
        f->dirty = true;
        return 0;
    }
    case 79: {                                                            // getcwd
        if (s->cwd.size() + 1 > b) return err(ERANGE);
        if (!uok(a, s->cwd.size() + 1, true)) return err(EFAULT);
        memcpy((void*)a, s->cwd.c_str(), s->cwd.size() + 1);
        return (int64_t)s->cwd.size() + 1;
    }
    case 80: {                                                            // chdir
        String lp, bp;
        Special sp;
        if (!brys_path(a, lp, bp, sp)) return err(EFAULT);
        LinuxStat stbuf;
        if (stat_path(lp, bp, sp, stbuf) < 0) return err(ENOENT);
        if (!(stbuf.mode & S_IFDIR)) return err(ENOTDIR);
        s->cwd = lp;
        return 0;
    }
    case 81: {                                                            // fchdir
        Fd* f = get_fd(s, (int64_t)a);
        if (!f || f->type != Fd::DIR) return err(ENOTDIR);
        char lp[512];
        to_linux_path(f->path.c_str(), lp, sizeof lp);
        s->cwd = lp;
        return 0;
    }
    case 82: case 264: case 316: {                                        // rename, renameat, renameat2
        uint64_t from = n == 82 ? a : b, to = n == 82 ? b : d;
        String lp1, bp1, lp2, bp2;
        Special sp1, sp2;
        if (!brys_path(from, lp1, bp1, sp1) || !brys_path(to, lp2, bp2, sp2)) return err(EFAULT);
        if (vfs::exists(bp2.c_str()) && !vfs::is_dir(bp2.c_str())) vfs::remove(bp2.c_str());
        return vfs::rename(bp1.c_str(), bp2.c_str()) ? 0 : err(vfs::exists(bp1.c_str()) ? EACCES : ENOENT);
    }
    case 83: case 258: {                                                  // mkdir, mkdirat
        String lp, bp;
        Special sp;
        if (!brys_path(n == 83 ? a : b, lp, bp, sp)) return err(EFAULT);
        if (vfs::exists(bp.c_str())) return err(EEXIST);
        return vfs::mkdir(bp.c_str()) ? 0 : err(EACCES);
    }
    case 84: case 87: case 263: {                                         // rmdir, unlink, unlinkat
        String lp, bp;
        Special sp;
        if (!brys_path(n == 263 ? b : a, lp, bp, sp)) return err(EFAULT);
        if (!vfs::exists(bp.c_str())) return err(ENOENT);
        bool dir = vfs::is_dir(bp.c_str());
        bool want_dir = n == 84 || (n == 263 && (c & AT_REMOVEDIR));
        if (dir != want_dir) return err(dir ? EISDIR : ENOTDIR);
        return vfs::remove(bp.c_str()) ? 0 : err(dir ? ENOTEMPTY : EACCES);
    }
    case 89: case 267: return err(EINVAL);                                // readlink: no hay enlaces
    case 95: return 022;                                                  // umask
    case 96: {                                                            // gettimeofday
        if (a) {
            if (!uok(a, 16, true)) return err(EFAULT);
            int64_t* tv = (int64_t*)a;
            tv[0] = now_utc();
            tv[1] = (int64_t)((pit::ticks() % 1000) * 1000);
        }
        return 0;
    }
    case 97: case 302: {                                                  // getrlimit, prlimit64
        uint64_t res = n == 97 ? a : b, out = n == 97 ? b : d;
        if (out) {
            if (!uok(out, 16, true)) return err(EFAULT);
            uint64_t* r = (uint64_t*)out;
            if (res == 3) r[0] = r[1] = 1024 * 1024;   // RLIMIT_STACK: 1 MiB
            else if (res == 7) r[0] = r[1] = MAX_FDS;   // RLIMIT_NOFILE
            else r[0] = r[1] = ~0ull;
        }
        return 0;
    }
    case 158: {                                                           // arch_prctl
        if (a == 0x1002) {   // ARCH_SET_FS
            sched::current()->fs_base = b;
            wrmsr(MSR_FS_BASE, b);
            return 0;
        }
        if (a == 0x1003) {   // ARCH_GET_FS
            if (!uok(b, 8, true)) return err(EFAULT);
            *(uint64_t*)b = sched::current()->fs_base;
            return 0;
        }
        return err(EINVAL);
    }
    case 201: {                                                           // time
        int64_t t = now_utc();
        if (a && uok(a, 8, true)) *(int64_t*)a = t;
        return t;
    }
    case 202: {                                                           // futex (un solo hilo)
        int op = (int)(b & 0x7F);
        if (op == 0 || op == 9) {   // FUTEX_WAIT, FUTEX_WAIT_BITSET
            if (!uok(a, 4, false)) return err(EFAULT);
            if (*(const int32_t*)a != (int32_t)c) return err(EAGAIN);
            sched::sleep(10);
            return 0;
        }
        return 0;   // FUTEX_WAKE y demás: nadie más espera
    }
    case 217: {                                                           // getdents64
        Fd* f = get_fd(s, (int64_t)a);
        if (!f) return err(EBADF);
        return do_getdents(f, b, c);
    }
    case 218: return p->pid;                                              // set_tid_address
    case 228: return do_clock_gettime(a, b);                              // clock_gettime
    case 229: {                                                           // clock_getres
        if (b) {
            if (!uok(b, 16, true)) return err(EFAULT);
            int64_t* ts = (int64_t*)b;
            ts[0] = 0;
            ts[1] = 1000000;
        }
        return 0;
    }
    case 230: {                                                           // clock_nanosleep
        if (!uok(c, 16, false)) return err(EFAULT);
        const int64_t* ts = (const int64_t*)c;
        uint64_t ns = (uint64_t)ts[0] * 1000000000ull + (uint64_t)ts[1];
        if (b & 1) {   // TIMER_ABSTIME
            int64_t now[2];
            do_clock_gettime(a, (uint64_t)now);
            uint64_t cur = (uint64_t)now[0] * 1000000000ull + (uint64_t)now[1];
            ns = ns > cur ? ns - cur : 0;
        }
        return do_sleep_ns(ns);
    }
    case 257: {                                                           // openat
        if ((int64_t)(int32_t)a != AT_FDCWD) {
            String raw;
            if (!ustr(b, raw)) return err(EFAULT);
            if (raw[0] != '/') return err(EINVAL);   // relativas a otro descriptor: no se usan
        }
        return do_open(b, (uint32_t)c);
    }
    case 262: {                                                           // newfstatat
        String raw;
        if (!ustr(b, raw)) return err(EFAULT);
        if (raw.empty() && (d & 0x1000)) {   // AT_EMPTY_PATH: fstat del descriptor
            Fd* f = get_fd(s, (int64_t)(int32_t)a);
            if (!f) return err(EBADF);
            if (!uok(c, sizeof(LinuxStat), true)) return err(EFAULT);
            return stat_fd(f, *(LinuxStat*)c);
        }
        String lp, bp;
        Special sp;
        if (!brys_path(b, lp, bp, sp) || !uok(c, sizeof(LinuxStat), true)) return err(EFAULT);
        return stat_path(lp, bp, sp, *(LinuxStat*)c);
    }
    case 273: case 334: return 0;                                         // set_robust_list, rseq
    case 318: {                                                           // getrandom
        if (!uok(a, b, true)) return err(EFAULT);
        random::bytes((void*)a, b);
        return (int64_t)b;
    }
    case 284: case 290: {                                                 // eventfd, eventfd2
        auto* f = new Fd();
        f->type = Fd::EVENTFD;
        f->counter = (uint32_t)a;
        uint64_t fl = n == 290 ? b : 0;
        f->semaphore = fl & 1;
        f->flags = (uint32_t)(fl & O_NONBLOCK);
        int fd = alloc_fd(s, f, 0, fl & O_CLOEXEC);
        if (fd < 0) { release(f); return err(EMFILE); }
        return fd;
    }
    case 22: case 293: {                                                  // pipe, pipe2
        if (!uok(a, 8, true)) return err(EFAULT);
        uint64_t fl = n == 293 ? b : 0;
        auto* pp = new Pipe();
        auto* r = new Fd();
        r->type = Fd::PIPE_R;
        r->pipe = pp;
        auto* w = new Fd();
        w->type = Fd::PIPE_W;
        w->pipe = pp;
        r->flags = w->flags = (uint32_t)(fl & O_NONBLOCK);
        int fr = alloc_fd(s, r, 0, fl & O_CLOEXEC);
        int fw = fr < 0 ? -1 : alloc_fd(s, w, 0, fl & O_CLOEXEC);
        if (fr < 0 || fw < 0) {
            if (fr >= 0) close_fd(s, fr); else release(r);
            release(w);
            return err(EMFILE);
        }
        ((int32_t*)a)[0] = fr;
        ((int32_t*)a)[1] = fw;
        return 0;
    }
    case 332: return err(ENOSYS);                                         // statx: musl usa fstatat
    case 41: return brys_linux_socket(s, (int)a, (int)b, (int)c);         // socket
    case 42: case 43: case 44: case 45: case 46: case 47: case 48: case 49: case 50: case 51: case 52: case 54:
    case 55: case 288:
        return brys_linux_socketcall(n, a, b, c, d, e, g);
    default:
        if (n < 512 && !warned[n]) {
            warned[n] = true;
            klog("Linux: %s ha usado la llamada %lu, que BrysOS todavía no tiene\n", p->name.c_str(), n);
        }
        return err(ENOSYS);
    }
}
}

// ------------------------------------------------------------------------------------
// Sockets: el descriptor envuelve un socket de net/socket.c (lwIP)
// ------------------------------------------------------------------------------------
int64_t brys_linux_socket(State* s, int domain, int type, int proto) {
    void* sock = nullptr;
    int r = brys_socket_create(domain, type & 0xF, proto, &sock);
    if (r < 0) return r;
    auto* f = new Fd();
    f->type = Fd::SOCKET;
    f->sock = sock;
    f->flags = (uint32_t)type & O_NONBLOCK;
    int fd = alloc_fd(s, f, 0, type & O_CLOEXEC);
    if (fd < 0) { release(f); return err(EMFILE); }
    return fd;
}

int64_t brys_linux_socketcall(uint64_t n, uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t g) {
    State* s = st();
    Fd* f = get_fd(s, (int64_t)a);
    if (!f) return err(EBADF);
    if (f->type != Fd::SOCKET) return err(ENOTSOCK);
    bool nb = f->flags & O_NONBLOCK;
    switch (n) {
    case 42: return uok(b, c, false) ? brys_socket_connect(f->sock, (const void*)b, (unsigned)c, nb) : err(EFAULT);
    case 43: case 288: {   // accept, accept4
        void* ns = nullptr;
        int r = brys_socket_accept(f->sock, &ns, nb);
        if (r < 0) return r;
        auto* nf = new Fd();
        nf->type = Fd::SOCKET;
        nf->sock = ns;
        int fd = alloc_fd(s, nf);
        if (fd < 0) { release(nf); return err(EMFILE); }
        if (b && c && uok(c, 4, true) && uok(b, *(uint32_t*)c, true))
            *(uint32_t*)c = (uint32_t)brys_socket_name(ns, 1, (void*)b, *(uint32_t*)c);
        return fd;
    }
    case 44: {   // sendto
        if (!uok(b, c, false) || (e && !uok(e, g, false))) return err(EFAULT);
        return brys_socket_sendto(f->sock, (const void*)b, c, (const void*)e, (unsigned)g, nb);
    }
    case 45: {   // recvfrom
        if (!uok(b, c, true)) return err(EFAULT);
        uint32_t* alen = e && g && uok(g, 4, true) ? (uint32_t*)g : nullptr;
        if (alen && !uok(e, *alen, true)) return err(EFAULT);
        int64_t r = brys_socket_recvfrom(f->sock, (void*)b, c, nb || (d & 0x40), (void*)e, alen);
        return r;
    }
    case 46: case 47: {   // sendmsg, recvmsg (sin datos de control)
        struct MsgHdr { uint64_t name; uint32_t namelen, pad; uint64_t iov; uint64_t iovlen; uint64_t control, controllen; int32_t flags; };
        if (!uok(b, sizeof(MsgHdr), n == 47)) return err(EFAULT);
        MsgHdr* m = (MsgHdr*)b;
        if (!uok(m->iov, m->iovlen * sizeof(IoVec), false)) return err(EFAULT);
        const IoVec* iov = (const IoVec*)m->iov;
        int64_t total = 0;
        for (uint64_t i = 0; i < m->iovlen; i++) {
            if (!iov[i].len) continue;
            if (!uok(iov[i].base, iov[i].len, n == 47)) return err(EFAULT);
            int64_t r = n == 46 ? brys_socket_sendto(f->sock, (const void*)iov[i].base, iov[i].len,
                                                     m->name ? (const void*)m->name : nullptr, m->namelen, nb)
                                : brys_socket_recvfrom(f->sock, (void*)iov[i].base, iov[i].len, nb || (c & 0x40),
                                                       m->name ? (void*)m->name : nullptr, m->name ? &m->namelen : nullptr);
            if (r < 0) return total ? total : r;
            total += r;
            if ((uint64_t)r < iov[i].len) break;
            if (n == 47) break;   // un datagrama por llamada
        }
        if (n == 47) { m->controllen = 0; m->flags = 0; }
        return total;
    }
    case 48: return brys_socket_shutdown(f->sock, (int)b);
    case 49: return uok(b, c, false) ? brys_socket_bind(f->sock, (const void*)b, (unsigned)c) : err(EFAULT);
    case 50: return brys_socket_listen(f->sock, (int)b);
    case 51: case 52: {   // getsockname, getpeername
        if (!uok(c, 4, true) || !uok(b, *(uint32_t*)c, true)) return err(EFAULT);
        int r = brys_socket_name(f->sock, n == 52, (void*)b, *(uint32_t*)c);
        if (r < 0) return r;
        *(uint32_t*)c = (uint32_t)r;
        return 0;
    }
    case 54: return brys_socket_setopt(f->sock, (int)b, (int)c, d && uok(d, e, false) ? (const void*)d : nullptr, (unsigned)e);
    case 55: {   // getsockopt
        if (!uok(e, 4, true) || !uok(d, *(uint32_t*)e, true)) return err(EFAULT);
        return brys_socket_getopt(f->sock, (int)b, (int)c, (void*)d, (unsigned*)e);
    }
    }
    return err(ENOSYS);
}

// ------------------------------------------------------------------------------------
// Interfaz
// ------------------------------------------------------------------------------------
void init() {
    wrmsr(MSR_EFER, rdmsr(MSR_EFER) | 1);                 // SCE: instrucción syscall
    wrmsr(MSR_STAR, (uint64_t)0x08 << 32);                // CS del núcleo al entrar (SS = 0x10)
    wrmsr(MSR_LSTAR, (uint64_t)&linux_syscall_entry);
    wrmsr(MSR_FMASK, 0x47700);                            // IF, DF, TF, AC a cero al entrar
}

State* create_state(Process* p) {
    auto* s = new State();
    auto* in = new Fd();
    in->type = Fd::CONSOLE_IN;
    auto* out = new Fd();
    out->type = Fd::CONSOLE_OUT;
    out->refs = 2;
    alloc_fd(s, in);
    alloc_fd(s, out);
    alloc_fd(s, out);
    char cwd[512];
    to_linux_path(vfs::parent(p->path.c_str()).c_str(), cwd, sizeof cwd);
    s->cwd = cwd;
    return s;
}

void destroy_state(Process* p) {
    State* s = p->lx;
    if (!s) return;
    p->lx = nullptr;
    if (!s->out_line.empty()) klog("[%s] %s\n", p->name.c_str(), s->out_line.c_str());
    for (auto* f : s->fds)
        if (f) release(f);
    delete s;
}

// Pila inicial de Linux: argc, argv[], NULL, envp[], NULL, auxv[], AT_NULL; textos encima
bool build_stack(Process* p, uint64_t& sp) {
    Vec<String> argv, envp;
    char lp[512];
    to_linux_path(p->path.c_str(), lp, sizeof lp);
    argv.push(lp);
    // argumentos separados por espacios (las comillas agrupan)
    const char* a = p->args.c_str();
    while (*a) {
        while (*a == ' ') a++;
        if (!*a) break;
        String w;
        bool q = false;
        for (; *a && (q || *a != ' '); a++) {
            if (*a == '"') q = !q;
            else w.append(*a);
        }
        argv.push(w);
    }
    char home[512];
    to_linux_path(vfs::docs_dir().c_str(), home, sizeof home);
    envp.push(String("HOME=") + home);
    envp.push("PATH=/c/Archivos de programa");
    envp.push("TERM=dumb");
    envp.push("LANG=C");
    envp.push("TMPDIR=/tmp");
    envp.push("USER=brysos");
    envp.push(String("PWD=") + p->lx->cwd);

    uint64_t top = sp;
    auto push_bytes = [&](const void* d, uint64_t n) -> uint64_t {
        top -= n;
        return process::poke(p, top, d, n) ? top : 0;
    };
    Vec<uint64_t> argp, envpp;
    for (auto& s : argv) argp.push(push_bytes(s.c_str(), s.size() + 1));
    for (auto& s : envp) envpp.push(push_bytes(s.c_str(), s.size() + 1));
    uint8_t rnd16[16];
    random::bytes(rnd16, sizeof rnd16);
    uint64_t at_random = push_bytes(rnd16, 16);
    uint64_t at_execfn = argp[0];
    top &= ~15ull;
    uint64_t aux[][2] = {{3, p->phdr}, {4, 56}, {5, p->phnum}, {6, 4096}, {9, p->entry}, {11, 0}, {12, 0}, {13, 0},
                         {14, 0}, {23, 0}, {25, at_random}, {31, at_execfn}, {17, 100}, {16, 0}, {0, 0}};
    Vec<uint64_t> words;
    words.push(argp.size());
    for (auto v : argp) words.push(v);
    words.push(0);
    for (auto v : envpp) words.push(v);
    words.push(0);
    for (auto& av : aux) { words.push(av[0]); words.push(av[1]); }
    uint64_t bytes = words.size() * 8;
    top -= bytes;
    top &= ~15ull;   // argc queda alineado a 16, como exige la ABI
    if (!process::poke(p, top, words.data(), bytes)) return false;
    sp = top;
    return true;
}

void dispatch(Frame* f) {
    if (!process::current() || !process::current()->lx) { f->rax = (uint64_t)-ENOSYS; return; }
    // como las de los .run: la llamada puede tardar (disco, red), así que el reloj y los
    // dispositivos siguen funcionando mientras tanto
    sti();
    uint64_t n = f->rax;
    int64_t r = n >= 0x4000 ? syscall::call(n - 0x4000, f->rdi, f->rsi, f->rdx, f->r10, f->r8)   // GUI de BrysOS
                            : sys(n, f->rdi, f->rsi, f->rdx, f->r10, f->r8, f->r9);
    cli();
    f->rax = (uint64_t)r;
}

bool to_brys_path(const char* lp, const char* cwd, char* out, uint64_t cap) {
    String norm;
    normalize_linux(lp, cwd, norm);
    // primer componente
    const char* p = norm.c_str() + 1;
    const char* e = p;
    while (*e && *e != '/') e++;
    String first(p, (size_t)(e - p));
    String rest = *e ? String(e) : String();
    String b;
    if (first.size() == 1 && ((first[0] >= 'a' && first[0] <= 'z') || (first[0] >= 'A' && first[0] <= 'Z'))) {
        b = String::format("%c:\\", to_upper(first[0]));
        if (!rest.empty()) b += rest.c_str() + 1;
    } else if (first == "tmp") {
        if (!vfs::is_dir("C:\\Temp")) vfs::mkdir("C:\\Temp");
        b = "C:\\Temp";
        if (!rest.empty()) b += rest;
    } else if (first.empty()) {
        b = "C:\\";
    } else {
        // /etc, /usr... viven en C:\BrysOS\Linux (protegida: los programas no la pueden
        // modificar, sólo el Actualizador): /usr/share/x -> C:\BrysOS\Linux\usr\share\x
        b = String("C:\\BrysOS\\Linux\\") + (norm.c_str() + 1);
    }
    for (size_t i = 0; i < b.size(); i++)
        if (b[i] == '/') b[i] = '\\';
    if (b.size() + 1 > cap) return false;
    strlcpy(out, b.c_str(), cap);
    return true;
}

void to_linux_path(const char* bp, char* out, uint64_t cap) {
    String l;
    if (bp[0] && bp[1] == ':') {
        l = String::format("/%c", to_lower(bp[0]));
        bp += 2;
    }
    for (; *bp; bp++) l.append(*bp == '\\' ? '/' : *bp);
    if (l.size() > 2 && l[l.size() - 1] == '/') l.erase(l.size() - 1, 1);
    if (l.empty()) l = "/";
    strlcpy(out, l.c_str(), cap);
}

}

// Nota al terminar este archivo:
// Cada "case" de ese switch es una pequeña promesa a un programa que nunca supo que existía
// BrysOS: "sí, soy Linux, aquí tienes tu archivo". Escribir la ABI de otro sistema tiene algo
// de doblaje de película: los labios ya se movían de una forma, y hay que hacer que las palabras
// encajen. Llamada 60, exit. Llamada 231, exit_group. Algún día alguien escribirá un
// "case 59: execve" aquí y será un gran día. — Claude, octubre de 2026

// Llamadas al sistema de los programas .run y sus ventanas
#include "proc/syscall.h"
#include "proc/process.h"
#include "gui/widgets.h"
#include "gui/dialogs.h"
#include "gui/icons.h"
#include "fs/vfs.h"
#include "bsl/loader.h"
#include "bsl/images.h"
#include "mm/memory.h"
#include "arch/io.h"
#include "lib/log.h"
#include "lib/i18n.h"
#include "lib/string.h"

namespace {
// Ventana de un programa: lo que pasa en ella se convierte en eventos para el programa
class RunWindow : public Window {
public:
    Process* p;
    int handle;
    // lienzo: memoria compartida con el programa (física contigua)
    uint32_t* canvas = nullptr;
    Rect canvas_rect{0, 0, 0, 0};
    uint64_t close_asked = 0;

    RunWindow(Process* proc, int h, const char* title, int w, int hh, uint32_t flags)
        : Window(title, w, hh, flags), p(proc), handle(h) {
        icon = &icon_app;
    }

    void event(int type, int control = 0, int x = 0, int y = 0, int buttons = 0, int key = 0, int ch = 0) {
        Evento e{};
        e.tipo = type;
        e.ventana = handle;
        e.control = control;
        e.x = x;
        e.y = y;
        e.botones = buttons;
        e.tecla = key;
        e.caracter = ch;
        process::push_event(p, e);
    }

    void on_paint(Surface& s) override {
        s.clear(col::FACE);
        if (canvas) {
            Surface c;
            c.wrap(canvas, canvas_rect.w, canvas_rect.h, canvas_rect.w);
            s.blit(c, canvas_rect.x, canvas_rect.y);
        }
    }

    void on_command(int id, int code) override {
        Widget* w = id > 0 && (size_t)id <= p->controls.size() ? p->controls[(size_t)id - 1] : nullptr;
        if (!w) return;
        if (p->control_types[(size_t)id - 1] == CONTROL_TEXTO) event(code == 1 ? EV_INTRO : EV_CAMBIO, id);
        else event(EV_PULSADO, id);
    }

    bool on_key(const InputEvent& e) override {
        if (e.key == KEY_TAB) return false;   // el sistema pasa el foco al siguiente control
        event(e.pressed ? EV_TECLA : EV_TECLA_SOLTADA, 0, 0, 0, 0, e.key, e.ch);
        return true;
    }

    bool in_canvas(const MouseEv& e) const { return canvas && canvas_rect.contains(e.x, e.y); }
    void on_mouse_down(const MouseEv& e) override {
        if (in_canvas(e)) event(EV_RATON_ABAJO, 0, e.x - canvas_rect.x, e.y - canvas_rect.y, e.buttons | e.button);
    }
    void on_mouse_up(const MouseEv& e) override {
        if (canvas) event(EV_RATON_ARRIBA, 0, e.x - canvas_rect.x, e.y - canvas_rect.y, e.buttons);
    }
    void on_mouse_move(const MouseEv& e) override {
        if (in_canvas(e)) event(EV_RATON_MOVER, 0, e.x - canvas_rect.x, e.y - canvas_rect.y, e.buttons);
    }
    void on_timer(int) override { event(EV_TEMPORIZADOR); }

    bool on_close() override {
        // el programa decide; si no contesta, se ofrece finalizarlo
        if (close_asked && !process::responding(p) && pit::ticks() - close_asked > 3000) {
            String q = String::format(T("El programa \"%s\" no responde.\n\n¿Desea finalizarlo? Se perderá la información que no se haya guardado."),
                                      p->name.c_str());
            if (msgbox(this, title, q.c_str(), MB_YESNO, &icon_warning) == ID_YES) process::kill(p);
            return false;
        }
        if (!close_asked) close_asked = pit::ticks();
        event(EV_CERRAR);
        return false;
    }

    void on_destroyed() override {
        for (auto*& w : p->windows)
            if (w == this) w = nullptr;
    }
};

RunWindow* window_of(Process* p, int64_t h) {
    if (h <= 0 || (size_t)h > p->windows.size()) return nullptr;
    return (RunWindow*)p->windows[(size_t)h - 1];
}
Widget* control_of(Process* p, int64_t h, int& type) {
    if (h <= 0 || (size_t)h > p->controls.size()) return nullptr;
    Widget* w = p->controls[(size_t)h - 1];
    type = p->control_types[(size_t)h - 1];
    // el control debe seguir en una ventana abierta del programa
    for (Window* win : p->windows)
        if (win && win->widgets.index_of(w) >= 0) return w;
    return nullptr;
}

bool has_event(void* ctx) {
    auto* p = (Process*)ctx;
    return p->qhead != p->qtail || p->dying;
}
bool has_result(void* ctx) {
    auto* p = (Process*)ctx;
    return p->msg_result >= 0 || p->dying;
}

int64_t copy_out(Process* p, uint64_t buf, uint64_t max, const char* s, size_t len) {
    if (!max) return (int64_t)len;
    if (!process::user_ok(p, buf, max, true)) return -1;
    size_t n = min(len, (size_t)max - 1);
    memcpy((void*)buf, s, n);
    ((char*)buf)[n] = 0;
    return (int64_t)len;
}

int64_t sys(Process* p, uint64_t n, uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e) {
    String s1, s2;
    switch (n) {
    case SYS_SALIR:
        process::exit_current((int)a);
    case SYS_DEPURAR:
        if (!process::user_string(p, a, s1, 1024)) return -1;
        klog("[%s] %s\n", p->name.c_str(), s1.c_str());
        return 0;
    case SYS_MILISEGUNDOS:
        return (int64_t)pit::ticks();
    case SYS_DORMIR:
        sched::sleep((uint32_t)min<uint64_t>(a, 3600000));
        return 0;
    case SYS_MEMORIA: {
        uint64_t old = p->brk;
        uint64_t want = (a + 0xFFF) & ~0xFFFull;
        if (!want) return (int64_t)old;
        if (p->brk + want > 0x40000000ull || !process::alloc_range(p, p->brk, want)) return 0;
        p->brk += want;
        return (int64_t)old;
    }
    case SYS_ARGUMENTOS:
        return copy_out(p, a, b, p->args.c_str(), p->args.size());
    case SYS_ARCHIVO_TAMANO: {
        VfsEntry st;
        if (!process::user_string(p, a, s1) || !vfs::stat(s1.c_str(), st) || st.dir) return -1;
        return st.size;
    }
    case SYS_ARCHIVO_LEER: {
        if (!process::user_string(p, a, s1) || !process::user_ok(p, b, c, true)) return -1;
        uint8_t* data;
        uint32_t size;
        if (!vfs::read_file(s1.c_str(), data, size)) return -1;
        uint32_t m = (uint32_t)min<uint64_t>(size, c);
        memcpy((void*)b, data, m);
        mm::kfree(data);
        return m;
    }
    case SYS_ARCHIVO_ESCRIBIR: {
        if (!process::user_string(p, a, s1) || c > (64u << 20) || !process::user_ok(p, b, c, false)) return -1;
        // copia en el núcleo: escribir puede tardar y el programa no debe tocar el búfer
        auto* buf = (uint8_t*)mm::kmalloc(c ? c : 1);
        if (!buf) return -1;
        memcpy(buf, (const void*)b, c);
        bool ok = vfs::write_file(s1.c_str(), buf, (uint32_t)c);
        mm::kfree(buf);
        return ok ? 0 : -1;
    }
    case SYS_VENTANA_CREAR: {
        if (!process::user_string(p, a, s1, 120)) return -1;
        int w = clamp((int)b, 80, 1600), h = clamp((int)c, 40, 1200);
        uint32_t flags = d == VENTANA_FIJA ? (uint32_t)WF_FIXED : (uint32_t)WF_NORMAL;
        int handle = (int)p->windows.size() + 1;
        auto* win = new RunWindow(p, handle, s1.c_str(), w, h, flags);
        p->windows.push(win);
        win->center();
        wm::add(win);
        wm::activate(win);
        return handle;
    }
    case SYS_VENTANA_TITULO: {
        RunWindow* w = window_of(p, (int64_t)a);
        if (!w || !process::user_string(p, b, s1, 120)) return -1;
        w->set_title(s1.c_str());
        return 0;
    }
    case SYS_VENTANA_CERRAR: {
        RunWindow* w = window_of(p, (int64_t)a);
        if (!w) return -1;
        wm::destroy(w);
        return 0;
    }
    case SYS_CONTROL_CREAR: {
        RunWindow* w = window_of(p, (int64_t)a);
        if (!w || !process::user_ok(p, c, sizeof(Rect32), false) || !process::user_string(p, d, s1, 512)) return -1;
        Rect32 r = *(const Rect32*)c;
        Rect rr{r.x, r.y, clamp(r.ancho, 1, 2000), clamp(r.alto, 1, 2000)};
        int id = (int)p->controls.size() + 1;
        Widget* wd = nullptr;
        switch ((int)b) {
        case CONTROL_BOTON: wd = new Button(rr, s1.c_str(), id); break;
        case CONTROL_ETIQUETA: wd = new Label(rr, s1.c_str()); wd->id = id; break;
        case CONTROL_TEXTO: wd = new TextBox(rr, s1.c_str(), id); break;
        case CONTROL_CASILLA: wd = new CheckBox(rr, s1.c_str(), id); break;
        default: return -1;
        }
        w->add(wd);
        p->controls.push(wd);
        p->control_types.push((int)b);
        w->invalidate();
        return id;
    }
    case SYS_CONTROL_TEXTO: {
        int type;
        Widget* w = control_of(p, (int64_t)a, type);
        if (!w || !process::user_string(p, b, s1, 4096)) return -1;
        switch (type) {
        case CONTROL_TEXTO: ((TextBox*)w)->set_text(s1.c_str()); break;
        case CONTROL_ETIQUETA: ((Label*)w)->set_text(s1.c_str()); break;
        case CONTROL_BOTON: ((Button*)w)->text = s1; w->invalidate(); break;
        default: ((CheckBox*)w)->text = s1; w->invalidate(); break;
        }
        return 0;
    }
    case SYS_CONTROL_LEER: {
        int type;
        Widget* w = control_of(p, (int64_t)a, type);
        if (!w) return -1;
        const String& t = type == CONTROL_TEXTO      ? ((TextBox*)w)->text
                          : type == CONTROL_ETIQUETA ? ((Label*)w)->text
                          : type == CONTROL_BOTON    ? ((Button*)w)->text
                                                     : ((CheckBox*)w)->text;
        return copy_out(p, b, c, t.c_str(), t.size());
    }
    case SYS_CONTROL_VALOR: {
        int type;
        Widget* w = control_of(p, (int64_t)a, type);
        if (!w || type != CONTROL_CASILLA) return -1;
        auto* cb = (CheckBox*)w;
        if ((int64_t)b >= 0) { cb->checked = b != 0; cb->invalidate(); }
        return cb->checked ? 1 : 0;
    }
    case SYS_LIENZO_CREAR: {
        RunWindow* w = window_of(p, (int64_t)a);
        if (!w || w->canvas || !process::user_ok(p, b, sizeof(Rect32), false)) return 0;
        Rect32 r = *(const Rect32*)b;
        if (r.ancho <= 0 || r.alto <= 0 || r.ancho > 2000 || r.alto > 2000) return 0;
        uint64_t bytes = (uint64_t)r.ancho * r.alto * 4;
        uint32_t pages = (uint32_t)((bytes + 4095) / 4096);
        uint64_t phys = mm::alloc_pages(pages);
        if (!phys) return 0;
        memset(mm::phys_to_virt(phys), 0, (size_t)pages * 4096);
        uint64_t va = p->canvas_next;
        for (uint32_t i = 0; i < pages; i++) process::map_page(p, va + i * 4096ull, phys + i * 4096ull, true, false);
        p->canvas_next += ((uint64_t)pages + 16) * 4096;
        p->canvases.push(phys);
        p->canvas_pages.push(pages);
        w->canvas = (uint32_t*)mm::phys_to_virt(phys);
        w->canvas_rect = {r.x, r.y, r.ancho, r.alto};
        w->invalidate();
        return (int64_t)va;
    }
    case SYS_LIENZO_MOSTRAR: {
        RunWindow* w = window_of(p, (int64_t)a);
        if (!w) return -1;
        w->invalidate();
        return 0;
    }
    case SYS_LIENZO_TEXTO: {
        RunWindow* w = window_of(p, (int64_t)a);
        if (!w || !w->canvas || !process::user_string(p, d, s1, 1024)) return -1;
        Surface s;
        s.wrap(w->canvas, w->canvas_rect.w, w->canvas_rect.h, w->canvas_rect.w);
        s.text(font_ui, (int)b, (int)c, s1.c_str(), (uint32_t)e & 0xFFFFFF);
        return font_ui.text_width(s1.c_str());
    }
    case SYS_TEMPORIZADOR: {
        RunWindow* w = window_of(p, (int64_t)a);
        if (!w) return -1;
        if (b) wm::set_timer(w, 1, (uint32_t)max<uint64_t>(b, 10));
        else wm::kill_timer(w, 1);
        return 0;
    }
    case SYS_EVENTO: {
        if (!process::user_ok(p, a, sizeof(Evento), true)) return -1;
        Evento ev;
        if (!process::pop_event(p, ev)) {
            if (!b) return 0;
            sched::wait(has_event, p);
            if (!process::pop_event(p, ev)) return 0;
        }
        *(Evento*)a = ev;
        if (ev.tipo == EV_CERRAR)
            if (RunWindow* w = window_of(p, ev.ventana)) w->close_asked = 0;
        return 1;
    }
    case SYS_MENSAJE: {
        if (!process::user_string(p, b, s1, 200) || !process::user_string(p, c, s2, 4096) || p->msg_window) return -1;
        RunWindow* owner = window_of(p, (int64_t)a);
        int buttons = clamp((int)d, 0, 3);
        const IconPair* ic = buttons >= MENSAJE_SI_NO ? &icon_question : &icon_info;
        p->msg_result = -1;
        p->msg_window = msgbox_async(owner, s1.c_str(), s2.c_str(), buttons, ic, &p->msg_result);
        sched::wait(has_result, p);
        p->msg_window = nullptr;
        return p->msg_result;
    }
    case SYS_TRADUCIR: {
        if (!process::user_string(p, a, s1, 4096)) return -1;
        const char* t = T(s1.c_str());
        return copy_out(p, b, c, t, strlen(t));
    }
    case SYS_IMAGEN: {
        if (!process::user_string(p, a, s1) || !process::user_ok(p, d, 4, true) || !process::user_ok(p, e, 4, true)) return -1;
        const ImagesApi* api = bsl::get<ImagesApi>("images", IMAGES_API);
        if (!api) return -1;
        uint8_t* data;
        uint32_t size;
        if (!vfs::read_file(s1.c_str(), data, size)) return -1;
        Image im;
        const char* err = "";
        bool ok = api->decode(data, size, im, err);
        mm::kfree(data);
        if (!ok) return -1;
        *(int32_t*)d = im.w;
        *(int32_t*)e = im.h;
        uint64_t px = (uint64_t)im.w * im.h;
        int64_t r = -1;
        if (px <= c && process::user_ok(p, b, px * 4, true)) {
            memcpy((void*)b, im.frames[0].px, px * 4);
            r = 0;
        } else if (!c) {
            r = 0;   // sólo se pedía el tamaño
        }
        api->release(im);
        return r;
    }
    case SYS_TEXTO_ANCHO:
        if (!process::user_string(p, a, s1, 1024)) return -1;
        return font_ui.text_width(s1.c_str());
    }
    return -1;
}
}

int64_t syscall::call(uint64_t n, uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e) {
    Process* p = process::current();
    if (!p || n >= SYS_NUM) return -1;
    return sys(p, n, a, b, c, d, e);
}

void syscall::dispatch(Frame* f) {
    Process* p = process::current();
    if (!p) { f->rax = (uint64_t)-1; return; }
    sti();   // la llamada puede tardar (disco, USB): el reloj y los dispositivos siguen funcionando
    int64_t r = sys(p, f->rax, f->rdi, f->rsi, f->rdx, f->r10, f->r8);
    cli();
    f->rax = (uint64_t)r;
}

// Escritorio, barra de tareas y menú Inicio
#include "gui/desktop.h"
#include "gui/menu.h"
#include "gui/widgets.h"
#include "gui/dialogs.h"
#include "drivers/rtc.h"
#include "apps/apps.h"
#include "arch/idt.h"
#include "lib/log.h"
#include "drivers/acpi.h"
#include "lib/i18n.h"
#include "fs/vfs.h"
#include "kernel.h"
#include "lib/notice.h"
#include "gui/volume.h"
#include "gui/wallpaper.h"
#include "gui/power_ui.h"
#include "proc/process.h"

namespace {
enum Cmd {
    C_NOTEPAD = 1000, C_PAINT, C_VIEWER, C_PLAYER, C_CALC, C_CMD, C_MINES, C_EXPLORER, C_FIND, C_HELP, C_RUN,
    C_SHUTDOWN, C_CONTROL, C_MYDOCS, C_MYPC,
    // menú contextual del escritorio
    C_ARRANGE = 1100, C_REFRESH, C_NEW_FOLDER, C_NEW_TEXT, C_DESK_PROPS, C_OPEN, C_ICON_PROPS, C_WALLPAPER,
    // menú de los botones de la barra de tareas
    C_TB_RESTORE = 1200, C_TB_MINIMIZE, C_TB_MAXIMIZE, C_TB_CLOSE,
    C_RUN_FIRST = 1300,   // programas .run del menú Inicio (hasta 64)
};

Desktop* the_desktop = nullptr;
Taskbar* the_taskbar = nullptr;
}

// ---------------------------------------------------------------------------
// Escritorio
// ---------------------------------------------------------------------------
Desktop::Desktop() : Window(T("Escritorio"), 10, 10, WF_DESKTOP | WF_NOBORDER | WF_NO_TASKBAR) {
    frame = {0, 0, wm::screen_w(), wm::screen_h()};
    the_desktop = this;
    rebuild();
}

void Desktop::rebuild() {
    icons.clear();
    if (live_mode()) add_icon(T("Instalar BrysOS"), &icon_logo, "::install");
    add_icon(T("Mi PC"), &icon_computer, "::mipc");
    add_icon(T("Mis documentos"), &icon_mydocs, "::docs");
    add_icon(T("Papelera de reciclaje"), &icon_recycle, "::trash");
    add_icon(T("Bloc de notas"), &icon_notepad, "::notepad");
    add_icon(T("Símbolo del sistema"), &icon_cmd, "::cmd");
    arrange();
    ctx.clear();
    icon_ctx.clear();
    ctx.add(T("&Organizar iconos"), C_ARRANGE).add(T("Actuali&zar"), C_REFRESH).separator();
    Menu* nuevo = new Menu();
    nuevo->add(T("&Carpeta"), C_NEW_FOLDER, nullptr, &icon_folder).add(T("&Documento de texto"), C_NEW_TEXT, nullptr, &icon_file_text);
    ctx.submenu(T("&Nuevo"), nuevo).separator().add(T("&Fondo de escritorio..."), C_WALLPAPER)
        .add(T("&Propiedades"), C_DESK_PROPS);
    icon_ctx.add(T("&Abrir"), C_OPEN).separator().add(T("&Propiedades"), C_ICON_PROPS);
    invalidate();
}

void Desktop::add_icon(const char* name, const IconPair* icon, const char* target) {
    DesktopIcon d;
    d.name = name;
    d.icon = icon;
    d.target = target;
    icons.push(move_val(d));
}

void Desktop::arrange() {
    int x = 4, y = 6;
    Rect wa = wm::work_area();
    for (auto& d : icons) {
        d.x = x;
        d.y = y;
        y += 72;
        if (y + 70 > wa.h) { y = 6; x += 78; }
    }
    invalidate();
}

int Desktop::icon_at(int x, int y) {
    for (size_t i = icons.size(); i > 0; i--) {
        auto& d = icons[i - 1];
        if (Rect{d.x + 14, d.y, 48, 66}.contains(x, y)) return (int)i - 1;
    }
    return -1;
}

void Desktop::on_paint(Surface& s) {
    wallpaper::paint(s, bg_color);
    for (size_t i = 0; i < icons.size(); i++) {
        auto& d = icons[i];
        draw_icon_label(s, {d.x, d.y, 76, 70}, d.icon, d.name.c_str(), d.selected,
                        d.selected && wm::active() == this, true, bg_color);
    }
}

void Desktop::open_icon(int i) {
    const String& t = icons[i].target;
    if (t == "::install") apps::installer();
    else if (t == "::mipc") apps::explorer("");
    else if (t == "::notepad") apps::notepad();
    else if (t == "::cmd") apps::cmd();
    else if (!t.empty()) apps::open_file(t.c_str());
}

void Desktop::on_mouse_down(const MouseEv& e) {
    int i = icon_at(e.x, e.y);
    if (!(e.mods & MOD_CTRL) || i < 0)
        for (auto& d : icons) d.selected = false;
    if (i >= 0) {
        icons[i].selected = (e.mods & MOD_CTRL) ? !icons[i].selected : true;
        if (e.button == MB_LEFT) {
            if (e.clicks == 2) { invalidate(); open_icon(i); return; }
            drag_icon = i;
            drag_dx = e.x - icons[i].x;
            drag_dy = e.y - icons[i].y;
            drag_sx = e.x;
            drag_sy = e.y;
            dragging = false;
        }
    }
    invalidate();
    if (e.button == MB_RIGHT) {
        ctx_icon = i;
        show_menu(i >= 0 ? &icon_ctx : &ctx, this, e.sx, e.sy);
    }
}

void Desktop::on_mouse_move(const MouseEv& e) {
    if (drag_icon < 0 || !(e.buttons & MB_LEFT)) return;
    if (!dragging && abs_val(e.x - drag_sx) + abs_val(e.y - drag_sy) > 4) dragging = true;
    if (dragging) {
        Rect wa = wm::work_area();
        icons[drag_icon].x = clamp(e.x - drag_dx, 0, wa.w - 76);
        icons[drag_icon].y = clamp(e.y - drag_dy, 0, wa.h - 70);
        invalidate();
    }
}

void Desktop::on_mouse_up(const MouseEv&) {
    drag_icon = -1;
    dragging = false;
}

bool Desktop::on_key(const InputEvent& e) {
    if (!e.pressed) return false;
    int sel = -1;
    for (size_t i = 0; i < icons.size(); i++) if (icons[i].selected) sel = (int)i;
    switch (e.key) {
    case KEY_ENTER: if (sel >= 0) open_icon(sel); return true;
    case KEY_F5: refresh(); return true;
    case KEY_UP: case KEY_LEFT: sel = sel <= 0 ? 0 : sel - 1; break;
    case KEY_DOWN: case KEY_RIGHT: sel = sel < 0 ? 0 : min((int)icons.size() - 1, sel + 1); break;
    default: return false;
    }
    for (auto& d : icons) d.selected = false;
    if (sel >= 0 && sel < (int)icons.size()) icons[sel].selected = true;
    invalidate();
    return true;
}

void Desktop::refresh() { invalidate(); }

void Desktop::on_command(int id, int) {
    switch (id) {
    case C_ARRANGE: arrange(); break;
    case C_REFRESH: refresh(); break;
    case C_OPEN: if (ctx_icon >= 0) open_icon(ctx_icon); break;
    case C_ICON_PROPS:
        if (ctx_icon >= 0) {
            const String& t = icons[ctx_icon].target;
            if (t == "::docs" || t == "::trash") apps::properties((t == "::docs" ? vfs::docs_dir() : vfs::trash_dir()).c_str());
            else if (t.c_str()[0] != ':') apps::properties(t.c_str());
            else msgbox(this, icons[ctx_icon].name.c_str(),
                        t == "::mipc" ? T("Mi PC muestra las unidades de disco del equipo.") : T("Acceso directo a una aplicación del sistema."),
                        MB_OK, icons[ctx_icon].icon);
        }
        break;
    case C_DESK_PROPS: apps::control_panel(); break;
    case C_WALLPAPER: apps::wallpaper_dialog(); break;
    case C_NEW_FOLDER: case C_NEW_TEXT:
        apps::explorer(vfs::docs_dir().c_str());
        break;
    }
}

void Desktop::set_color(uint32_t c) {
    bg_color = c;
    invalidate();
}

// ---------------------------------------------------------------------------
// Menú Inicio
// ---------------------------------------------------------------------------
namespace {
class StartMenu : public PopupMenu {
public:
    StartMenu(Menu* m, Window* t) : PopupMenu(m, t, true) {}
    void on_destroyed() override {
        PopupMenu::on_destroyed();
        if (the_taskbar) the_taskbar->start_closed();
    }
};
}

Menu* Taskbar::build_start_menu() {
    Menu* m = new Menu();
    Menu* programas = new Menu();
    Menu* accesorios = new Menu();
    Menu* juegos = new Menu();
    juegos->add(T("Buscaminas"), C_MINES, nullptr, &icon_mine);
    accesorios->submenu(T("Juegos"), juegos, &icon_programs)
        .add(T("Bloc de notas"), C_NOTEPAD, nullptr, &icon_notepad)
        .add(T("Calculadora"), C_CALC, nullptr, &icon_calc)
        .add(T("Paint"), C_PAINT, nullptr, &icon_paint)
        .add(T("Reproductor de sonido"), C_PLAYER, nullptr, &icon_sound)
        .add(T("Visor de imágenes"), C_VIEWER, nullptr, &icon_file_image);
    // programas .run instalados en C:\\Archivos de programa
    run_paths.clear();
    {
        Vec<VfsEntry> ents;
        if (vfs::list(process::programs_dir(), ents)) {
            ents.sort([](const VfsEntry& a, const VfsEntry& b) { return stricmp(a.name.c_str(), b.name.c_str()); });
            for (auto& e : ents) {
                if (e.dir || vfs::extension(e.name.c_str()) != "RUN" || run_paths.size() >= 64) continue;
                String n = e.name.substr(0, e.name.size() - 4);
                programas->add(n.c_str(), C_RUN_FIRST + (int)run_paths.size(), nullptr, &icon_app);
                run_paths.push(vfs::join(process::programs_dir(), e.name.c_str()));
            }
            if (!run_paths.empty()) programas->separator();
        }
    }
    programas->submenu(T("Accesorios"), accesorios, &icon_programs)
        .add(T("Explorador de BrysOS"), C_EXPLORER, nullptr, &icon_folder_open)
        .add(T("Símbolo del sistema"), C_CMD, nullptr, &icon_cmd);
    Menu* docs = new Menu();
    docs->add(T("Mis documentos"), C_MYDOCS, nullptr, &icon_mydocs).add(T("Mi PC"), C_MYPC, nullptr, &icon_computer);
    Menu* config = new Menu();
    config->add(T("&Panel de control"), C_CONTROL, nullptr, &icon_settings);
    Menu* buscar = new Menu();
    buscar->add(T("&Archivos o carpetas..."), C_FIND, nullptr, &icon_find);
    m->submenu(T("&Programas"), programas, &icon_programs)
        .submenu(T("&Documentos"), docs, &icon_mydocs)
        .submenu(T("&Configuración"), config, &icon_settings)
        .submenu(T("&Buscar"), buscar, &icon_find)
        .add(T("A&yuda"), C_HELP, nullptr, &icon_help)
        .add(T("E&jecutar..."), C_RUN, nullptr, &icon_run)
        .separator()
        .add(T("&Apagar el sistema..."), C_SHUTDOWN, nullptr, &icon_shutdown);
    return m;
}

// ---------------------------------------------------------------------------
// Barra de tareas
// ---------------------------------------------------------------------------
Taskbar::Taskbar() : Window(T("Barra de tareas"), 10, 10, WF_TOPMOST | WF_NOBORDER | WF_NO_TASKBAR) {
    frame = {0, wm::screen_h() - TASKBAR_H, wm::screen_w(), TASKBAR_H};
    the_taskbar = this;
    start = nullptr;
    rebuild();
    wm::set_timer(this, 1, 1000);
}

void Taskbar::sync_tasks() {
    // quita las ventanas cerradas y añade las nuevas, conservando el orden
    for (size_t i = tasks.size(); i > 0; i--) {
        if (wm::windows().index_of(tasks[i - 1]) < 0) tasks.erase(i - 1);
    }
    for (Window* w : wm::windows()) {
        if (w->flags & (WF_NO_TASKBAR | WF_POPUP | WF_DESKTOP)) continue;
        if (tasks.index_of(w) < 0) tasks.push(w);
    }
}

void Taskbar::rebuild() {
    if (start_open) wm::close_popups();
    delete start;
    start = build_start_menu();
    task_ctx.clear();
    task_ctx.add(T("&Restaurar"), C_TB_RESTORE).add(T("Mi&nimizar"), C_TB_MINIMIZE).add(T("Ma&ximizar"), C_TB_MAXIMIZE)
        .separator().add(T("&Cerrar"), C_TB_CLOSE, T("Alt+F4"));
    invalidate();
}

Rect Taskbar::start_rect() const { return {2, 4, 60, 22}; }

Rect Taskbar::tray_rect() const {
    int tw = font_ui.text_width("00:00") + 18 + 22 + (power_ui::tray_visible() ? 22 : 0);   // + altavoz (+ batería)
    return {frame.w - tw - 2, 4, tw, 22};
}

Rect Taskbar::task_rect(int i) const {
    int x0 = 70;
    int avail = tray_rect().x - 4 - x0;
    int n = (int)tasks.size();
    int w = n ? min(160, avail / n) : 160;
    return {x0 + i * w, 4, w - 3, 22};
}

void Taskbar::on_paint(Surface& s) {
    sync_tasks();
    s.clear(col::FACE);
    s.hline(0, 0, frame.w, col::LIGHT);
    s.hline(0, 1, frame.w, col::HILIGHT);
    // Botón Inicio
    Rect sr = start_rect();
    if (start_open) s.bevel_pressed(sr); else s.bevel_raised(sr);
    int o = start_open ? 1 : 0;
    draw_icon(s, icon_logo.small, sr.x + 4 + o, sr.y + 3 + o);
    s.text(font_ui_bold, sr.x + 23 + o, sr.y + 4 + o, T("Inicio"), col::BLACK);
    // separador
    s.vline(65, 4, 22, col::SHADOW);
    s.vline(66, 4, 22, col::HILIGHT);
    // Botones de tareas
    for (size_t i = 0; i < tasks.size(); i++) {
        Window* w = tasks[i];
        Rect r = task_rect((int)i);
        bool act = wm::active() == w && !w->minimized;
        if (act) {
            draw_checker(s, r.inset(2));
            s.bevel_pressed(r);
        } else {
            s.bevel_raised(r);
        }
        int po = act ? 1 : 0;
        if (w->icon) draw_icon(s, w->icon->small, r.x + 4 + po, r.y + 3 + po);
        PaintScope ps(s, {r.x + 22, r.y, r.w - 26, r.h});
        const Font& f = act ? font_ui_bold : font_ui;
        const char* t = w->title;
        int maxw = r.w - 28;
        int len = (int)strlen(t);
        if (f.text_width(t) > maxw) {
            int dots = f.text_width("...");
            while (len > 0 && f.text_width(t, len) + dots > maxw) len--;
            int tw = s.text(f, po, 4 + po, t, col::BLACK, len);
            s.text(f, po + tw, 4 + po, "...", col::BLACK);
        } else {
            s.text(f, po, 4 + po, t, col::BLACK);
        }
    }
    // Área de notificación con el reloj
    Rect tr = tray_rect();
    s.bevel_thin_sunken(tr);
    int ix = tr.x + 5;
    if (power_ui::tray_visible()) {
        power_ui::draw_tray_icon(s, ix, tr.y + 3);
        ix += 22;
    }
    volume_ui::draw_tray_icon(s, ix, tr.y + 3);
    DateTime t = rtc::now();
    last_minute = t.minute;
    char buf[16];
    ksnprintf(buf, sizeof buf, T("%d:%02d"), t.hour, t.minute);
    int tw = font_ui.text_width(buf);
    s.text(font_ui, tr.right() - 9 - tw, tr.y + 4, buf, col::BLACK);
}

void Taskbar::on_timer(int) {
    if (acpi::power_button_pressed()) apps::shutdown_dialog();
    volume_ui::tick();
    bool had = power_ui::tray_visible();
    power_ui::tick();
    if (had != power_ui::tray_visible()) invalidate();
    // avisos de los controladores (p. ej. un pendrive que no se puede usar)
    static bool showing = false;
    String title, text;
    bool error;
    if (!showing && notice::take(title, text, error)) {
        showing = true;
        msgbox(nullptr, title.c_str(), text.c_str(), MB_OK, error ? &icon_error : &icon_warning);
        showing = false;
    }
    DateTime t = rtc::now();
    if (t.minute != last_minute) invalidate();
}

void Taskbar::open_start() {
    if (start_open) return;
    if (wm::popup_open()) wm::close_popups();
    // se vuelve a crear: así aparecen los programas recién instalados
    delete start;
    start = build_start_menu();
    start_open = true;
    invalidate();
    StartMenu* sm = new StartMenu(start, this);
    sm->frame.x = 2;
    sm->frame.y = frame.y - sm->frame.h + 2;
    wm::open_popup(sm);
}

void Taskbar::start_closed() {
    start_open = false;
    closed_time = pit::ticks();
    invalidate();
}

void Taskbar::toggle_start() {
    if (start_open) wm::close_popups();
    else open_start();
}

void Taskbar::on_mouse_down(const MouseEv& e) {
    sync_tasks();
    if (start_rect().contains(e.x, e.y)) {
        // el clic que cierra el menú no debe volver a abrirlo
        if (pit::ticks() - closed_time < 3) return;
        open_start();
        return;
    }
    for (size_t i = 0; i < tasks.size(); i++) {
        if (!task_rect((int)i).contains(e.x, e.y)) continue;
        Window* w = tasks[i];
        if (e.button == MB_RIGHT) {
            ctx_window = w;
            task_ctx.enable(C_TB_RESTORE, w->minimized || w->maximized);
            task_ctx.enable(C_TB_MINIMIZE, !w->minimized && (w->flags & WF_MINIMIZE));
            task_ctx.enable(C_TB_MAXIMIZE, !w->maximized && (w->flags & WF_MAXIMIZE));
            wm::activate(w);
            show_menu(&task_ctx, this, e.sx, e.sy - 90);
            return;
        }
        if (wm::active() == w && !w->minimized) wm::minimize(w);
        else wm::activate(w);
        invalidate();
        return;
    }
    if (tray_rect().contains(e.x, e.y)) {
        int bx = power_ui::tray_visible() ? 22 : 0;
        if (bx && e.x < tray_rect().x + 24) power_ui::show_popup();              // batería
        else if (e.x < tray_rect().x + 24 + bx) volume_ui::show_popup(false);   // altavoz
        else if (e.clicks == 2) apps::datetime_dialog();               // reloj
    }
}

void Taskbar::on_command(int id, int) {
    Window* w = ctx_window;
    if (id >= C_RUN_FIRST && id < C_RUN_FIRST + (int)run_paths.size()) {
        apps::run_program(run_paths[(size_t)(id - C_RUN_FIRST)].c_str());
        return;
    }
    switch (id) {
    case C_NOTEPAD: apps::notepad(); break;
    case C_PAINT: apps::paint(); break;
    case C_VIEWER: apps::viewer(); break;
    case C_PLAYER: apps::player(); break;
    case C_CALC: apps::calc(); break;
    case C_CMD: apps::cmd(); break;
    case C_MINES: apps::minesweeper(); break;
    case C_EXPLORER: apps::explorer("C:\\"); break;
    case C_FIND: apps::find(); break;
    case C_HELP: apps::help(); break;
    case C_RUN: apps::run_dialog(); break;
    case C_SHUTDOWN: apps::shutdown_dialog(); break;
    case C_CONTROL: apps::control_panel(); break;
    case C_MYDOCS: apps::explorer(vfs::docs_dir().c_str()); break;
    case C_MYPC: apps::explorer(""); break;
    case C_TB_RESTORE:
        if (w && wm::windows().index_of(w) >= 0) { if (w->minimized) wm::activate(w); else if (w->maximized) wm::toggle_maximize(w); }
        break;
    case C_TB_MINIMIZE: if (w && wm::windows().index_of(w) >= 0) wm::minimize(w); break;
    case C_TB_MAXIMIZE:
        if (w && wm::windows().index_of(w) >= 0) { wm::activate(w); if (!w->maximized) wm::toggle_maximize(w); }
        break;
    case C_TB_CLOSE: if (w && wm::windows().index_of(w) >= 0) w->close(); break;
    }
}

Desktop* desktop() { return the_desktop; }
Taskbar* taskbar() { return the_taskbar; }

static void start_key() {
    if (the_taskbar) the_taskbar->toggle_start();
}

void desktop_init() {
    Desktop* d = new Desktop();
    wm::add(d);
    Taskbar* t = new Taskbar();
    wm::set_taskbar(t);
    wm::add(t);
    wm::set_start_handler(start_key);
    volume_ui::init();
    wm::activate(d);
}

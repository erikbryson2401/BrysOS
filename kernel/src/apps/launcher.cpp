// Asociaciones de archivos y ejecución de órdenes
#include "apps/common.h"
#include "proc/process.h"

static bool is_text(const String& e) {
    const char* exts[] = {"TXT", "INI", "LOG", "CFG", "BAT", "C", "CPP", "H", "HPP", "MD", "CSV", "ASM", "PY", "JSON", "XML", "HTM", "HTML", "CSS", "JS", "INF", nullptr};
    for (int i = 0; exts[i]; i++) if (e == exts[i]) return true;
    return false;
}

namespace apps {
bool open_file(const char* p) {
    String path = vfs::normalize(p);
    if (vfs::is_dir(path.c_str())) { explorer(path.c_str()); return true; }
    if (!vfs::exists(path.c_str())) {
        msgbox(nullptr, T("Error"), String::format(T("No se puede encontrar '%s'."), path.c_str()).c_str(), MB_OK, &icon_error);
        return false;
    }
    String e = vfs::extension(path.c_str());
    if (is_text(e)) { notepad(path.c_str()); return true; }
    if (e == "BMP") { paint(path.c_str()); return true; }
    if (image_file(e.c_str())) { viewer(path.c_str()); return true; }
    if (sound_file(e.c_str())) { player(path.c_str()); return true; }
    if (e == "BSI") { updater(path.c_str()); return true; }
    if (e == "RUN") return run_program(path.c_str());
    String q = String::format(T("No hay ningún programa asociado a los archivos \"%s\".\n\n¿Desea abrirlo con el Bloc de notas?"),
                              e.empty() ? T("sin extensión") : e.c_str());
    if (msgbox(nullptr, T("Abrir con"), q.c_str(), MB_YESNO, &icon_question) == ID_YES) { notepad(path.c_str()); return true; }
    return false;
}

bool run_command(const char* cmdline) {
    while (*cmdline == ' ') cmdline++;
    // primer elemento (puede ir entre comillas) y argumento
    String prog, arg;
    const char* p = cmdline;
    if (*p == '"') { p++; while (*p && *p != '"') prog.append(*p++); if (*p) p++; }
    else while (*p && *p != ' ') prog.append(*p++);
    while (*p == ' ') p++;
    arg = p;
    if (arg.size() >= 2 && arg[0] == '"' && arg[arg.size() - 1] == '"') arg = arg.substr(1, arg.size() - 2);
    String n = prog;
    for (size_t i = 0; i < n.size(); i++) n[i] = to_upper(n[i]);
    if (n.size() > 4 && !strcmp(n.c_str() + n.size() - 4, ".EXE")) n = n.substr(0, n.size() - 4);
    const char* a = arg.empty() ? nullptr : arg.c_str();
    String full_arg = a ? vfs::normalize(a) : String();
    const char* fa = a ? full_arg.c_str() : nullptr;
    if (n == "NOTEPAD" || n == "BLOC" || n == "EDIT") { notepad(fa); return true; }
    if (n == "PAINT" || n == "MSPAINT" || n == "PBRUSH") { paint(fa); return true; }
    if (n == "CALC" || n == "CALCULADORA") { calc(); return true; }
    if (n == "CMD" || n == "COMMAND" || n == "SIMBOLO") { cmd(fa); return true; }
    if (n == "EXPLORER" || n == "EXPLORADOR" || n == "MIPC") { explorer(fa ? fa : "C:\\"); return true; }
    if (n == "WINMINE" || n == "BUSCAMINAS") { minesweeper(); return true; }
    if (n == "BUSCAR" || n == "FIND") { find(fa); return true; }
    if (n == "WINHELP" || n == "AYUDA") { help(); return true; }
    if (n == "CREDITOS" || n == "LICENCIAS" || n == "CREDITS" || n == "LICENSES") { credits(); return true; }
    if (n == "CONTROL" || n == "PANEL") { control_panel(); return true; }
    if (n == "VISOR" || n == "IMAGENES" || n == "VIEWER") { viewer(fa); return true; }
    if (n == "FONDO" || n == "WALLPAPER") { wallpaper_dialog(fa); return true; }
    if (n == "REPRODUCTOR" || n == "SNDREC32" || n == "PLAYER") { player(fa); return true; }
    // ruta a un archivo o carpeta
    if (cmdline[0] && (cmdline[1] == ':' || cmdline[0] == '\\' || cmdline[0] == '"')) {
        String whole = cmdline;
        if (whole.size() >= 2 && whole[0] == '"') whole = prog;
        String path = vfs::normalize(whole.c_str());
        if (vfs::exists(path.c_str())) return open_file(path.c_str());
        path = vfs::normalize(prog.c_str());
        if (vfs::exists(path.c_str())) {
            // programa .run con argumentos: "C:\ruta\prog.run archivo"
            if (vfs::extension(path.c_str()) == "RUN") return run_program(path.c_str(), a);
            return open_file(path.c_str());
        }
    }
    // nombre de un programa instalado en C:\Archivos de programa ("serpiente")
    if (!strchr(prog.c_str(), '\\') && !strchr(prog.c_str(), ':')) {
        String name = prog;
        if (vfs::extension(name.c_str()) != "RUN") name += ".run";
        Vec<VfsEntry> ents;
        if (vfs::list(process::programs_dir(), ents))
            for (auto& e : ents)
                if (!e.dir && !stricmp(e.name.c_str(), name.c_str()))   // con el nombre tal como está escrito
                    return run_program(vfs::join(process::programs_dir(), e.name.c_str()).c_str(), a);
    }
    return false;
}
}

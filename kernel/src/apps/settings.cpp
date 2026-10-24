#include "apps/settings.h"
#include "fs/vfs.h"
#include "gui/desktop.h"
#include "drivers/keyboard.h"
#include "drivers/rtc.h"
#include "lib/i18n.h"
#include "lib/string.h"
#include "sys/paths.h"
#include "fs/bootloader.h"
#include "drivers/audio.h"

namespace settings {
uint32_t desktop_color = col::DESKTOP;
int mouse_speed = 2;
char language[8] = "es";
char keyboard[16] = "es";
int utc_offset = 0;
bool rtc_utc = false;
bool intro_always = false;
int volume = 80;
bool muted = false;
int intro_volume = 100;
bool wallpaper = false;
int wallpaper_mode = 3;
int brightness = -1;
bool credits_hidden = false;
static const char* PATH = sys::CONFIG;

static int64_t parse_num(const char* s, int base) {
    bool neg = *s == '-';
    if (neg || *s == '+') s++;
    int64_t v = 0;
    for (; *s; s++) {
        char c = to_upper(*s);
        int d = (c >= '0' && c <= '9') ? c - '0' : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
        if (d < 0 || d >= base) break;
        v = v * base + d;
    }
    return neg ? -v : v;
}

void load() {
    uint8_t* data;
    uint32_t size;
    if (vfs::read_file(PATH, data, size)) {
        char* p = (char*)data;
        while (*p) {
            char* line = p;
            while (*p && *p != '\n') p++;
            if (*p) *p++ = 0;
            char* eq = line;
            while (*eq && *eq != '=') eq++;
            if (!*eq) continue;
            *eq = 0;
            char* val = eq + 1;
            for (char* q = val; *q; q++) if (*q == '\r') *q = 0;
            if (!strcmp(line, "ColorEscritorio")) desktop_color = (uint32_t)parse_num(val, 16) & 0xFFFFFF;
            else if (!strcmp(line, "VelocidadRaton")) mouse_speed = (int)parse_num(val, 10);
            else if (!strcmp(line, "Idioma")) strlcpy(language, val, sizeof language);
            else if (!strcmp(line, "Teclado")) strlcpy(keyboard, val, sizeof keyboard);
            else if (!strcmp(line, "ZonaHoraria")) utc_offset = (int)parse_num(val, 10);
            else if (!strcmp(line, "RelojUTC")) rtc_utc = parse_num(val, 10) != 0;
            else if (!strcmp(line, "PresentacionSiempre")) intro_always = parse_num(val, 10) != 0;
            else if (!strcmp(line, "Volumen")) volume = clamp((int)parse_num(val, 10), 0, 100);
            else if (!strcmp(line, "Silencio")) muted = parse_num(val, 10) != 0;
            else if (!strcmp(line, "VolumenPresentacion")) intro_volume = clamp((int)parse_num(val, 10), 0, 100);
            else if (!strcmp(line, "Fondo")) wallpaper = parse_num(val, 10) != 0;
            else if (!strcmp(line, "FondoModo")) wallpaper_mode = clamp((int)parse_num(val, 10), 0, 3);
            else if (!strcmp(line, "Brillo")) brightness = clamp((int)parse_num(val, 10), -1, 100);
            else if (!strcmp(line, "OcultarCreditos")) credits_hidden = parse_num(val, 10) != 0;
        }
        mm::kfree(data);
    }
    apply();
}

void serialize(char* out, int cap) {
    ksnprintf(out, (size_t)cap,
              "[BrysOS]\r\nIdioma=%s\r\nTeclado=%s\r\nZonaHoraria=%d\r\nRelojUTC=%d\r\nColorEscritorio=%06X\r\nVelocidadRaton=%d\r\n"
              "PresentacionSiempre=%d\r\nVolumen=%d\r\nSilencio=%d\r\nVolumenPresentacion=%d\r\nFondo=%d\r\nFondoModo=%d\r\nBrillo=%d\r\n"
              "OcultarCreditos=%d\r\n",
              language, keyboard, utc_offset, rtc_utc ? 1 : 0, desktop_color, mouse_speed, intro_always ? 1 : 0,
              volume, muted ? 1 : 0, intro_volume, wallpaper ? 1 : 0, wallpaper_mode, brightness, credits_hidden ? 1 : 0);
}

// Reescribe la línea "multiboot2" de C:\BrysOS\grub.cfg con el idioma y el volumen actuales
// y la marca "intro" que activa el logo con sonido en el siguiente arranque
// (intro: 1 = ponerla, 0 = quitarla, -1 = dejarla como esté).
static bool rewrite_boot_line(int intro) {
    const char* path = sys::GRUB_CFG;
    uint8_t* data;
    uint32_t size;
    if (!vfs::read_file(path, data, size)) return false;
    String in((const char*)data, size), out;
    mm::kfree(data);
    // bibliotecas que hay en C:\BrysOS\BSL: una línea module2 por cada una
    String modules;
    {
        Vec<VfsEntry> ents;
        Vec<String> files;
        if (vfs::list(sys::BSL_DIR, ents))
            for (auto& e : ents)
                if (!e.dir && vfs::extension(e.name.c_str()) == "BSL") files.push(e.name);
        modules = bootloader::bsl_module_lines(files);
    }
    bool changed = false;
    size_t pos = 0;
    while (pos < in.size()) {
        int nl = in.find('\n', pos);
        size_t end = nl < 0 ? in.size() : (size_t)nl + 1;
        String line = in.substr(pos, end - pos);
        const char* l = line.c_str();
        while (*l == ' ' || *l == '\t') l++;
        if (!strncmp(l, "multiboot2 ", 11) && !strncmp(l + 11, sys::GRUB_KERNEL, strlen(sys::GRUB_KERNEL))) {
            // conserva root=..., cambia el resto
            String indent = line.substr(0, (size_t)(l - line.c_str()));
            const char* r = l;
            String root;
            for (; *r; r++) {
                if (!strncmp(r, "root=", 5)) {
                    while (*r && *r != ' ' && *r != '\n' && *r != '\r') root.append(*r++);
                    break;
                }
            }
            bool has_intro = intro > 0 || (intro < 0 && strstr(l, " intro") != nullptr);
            String nline = indent + "multiboot2 " + sys::GRUB_KERNEL + " " + root + " lang=" + language +
                           String::format(" vol=%d introvol=%d", volume, intro_volume) + (muted ? " mute" : "") +
                           (has_intro ? " intro" : "") + "\n";
            if (nline != line.c_str()) changed = true;
            out += nline;
            out += modules;
            // las líneas de las bibliotecas que ya hubiera se sustituyen por las nuevas
            size_t p2 = end, old_start = end;
            while (p2 < in.size()) {
                int nl2 = in.find('\n', p2);
                size_t e2 = nl2 < 0 ? in.size() : (size_t)nl2 + 1;
                const char* l2 = in.c_str() + p2;
                while (*l2 == ' ' || *l2 == '\t') l2++;
                if (strncmp(l2, "module2 ", 8) || strncmp(l2 + 8, sys::GRUB_BSL_DIR, strlen(sys::GRUB_BSL_DIR))) break;
                p2 = e2;
            }
            if (in.substr(old_start, p2 - old_start) != modules.c_str()) changed = true;
            end = p2;
        } else {
            out += line;
        }
        pos = end;
    }
    if (!changed) return true;
    vfs::SystemWrite sw;
    return vfs::write_file(path, out.c_str(), (uint32_t)out.size());
}

bool set_boot_intro(bool intro) { return rewrite_boot_line(intro ? 1 : 0); }
bool update_boot_line() { return rewrite_boot_line(-1); }

bool save() {
    char buf[640];
    serialize(buf, sizeof buf);
    vfs::SystemWrite sw;
    return vfs::write_file(PATH, buf, (uint32_t)strlen(buf));
}

void apply() {
    i18n::set_language(i18n::from_code(language));
    if (!keyboard::set_layout(keyboard)) keyboard::set_layout("es");
    rtc::configure(rtc_utc, utc_offset);
    if (desktop()) desktop()->set_color(desktop_color);
    wm::set_mouse_speed(mouse_speed);
    audio::set_volume(volume);
    audio::set_mute(muted);
}
}

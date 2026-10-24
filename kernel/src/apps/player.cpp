// Reproductor de sonido (al estilo de la Grabadora de sonidos de Windows 98) con sonido.bsl
#include "apps/common.h"
#include "bsl/loader.h"
#include "bsl/sonido.h"
#include "drivers/audio.h"
#include "arch/io.h"

namespace {
enum { ID_START = 10, ID_PLAY, ID_PAUSE, ID_STOP, ID_OPEN, ID_SEEK };
constexpr int SCOPE = 256;

const SoundApi* sound() { return bsl::get<SoundApi>("sonido", SOUND_API); }

class Player;
Player* playing_now = nullptr;   // la tarjeta sólo reproduce un sonido a la vez

class Player : public Window {
public:
    const SoundApi* api;
    uint8_t* data = nullptr;
    Sound* snd = nullptr;
    SoundInfo info;
    String path;
    bool playing = false;
    uint64_t start_frame = 0;        // posición (tramas del archivo) al empezar a sonar
    uint64_t paused_at = 0;
    volatile int16_t scope[SCOPE];   // última forma de onda enviada a la tarjeta (circular)
    volatile uint32_t scope_pos = 0;
    TrackBar* seek;
    Label* pos_lbl;
    Label* len_lbl;
    Label* status;
    Button* play_btn;
    Button* pause_btn;

    Player() : Window(T("Reproductor de sonido"), 430, 214, WF_FIXED), api(sound()) {
        icon = &icon_sound;
        for (auto& v : scope) v = 0;
        add(new Label({12, 12, 100, 16}, T("Posición:")));
        pos_lbl = add(new Label({12, 30, 100, 16}, ""));
        add(new Label({318, 12, 100, 16}, T("Duración:"), 2));
        len_lbl = add(new Label({318, 30, 100, 16}, "", 2));
        seek = add(new TrackBar({8, 84, 414, 40}, ID_SEEK));
        seek->tick_every = 0;
        int x = 12;
        auto btn = [&](const char* t, int id, int w) {
            auto* b = add(new Button({x, 132, w, 25}, t, id));
            x += w + 6;
            return b;
        };
        btn(T("|< Inicio"), ID_START, 70);
        play_btn = btn(T("&Reproducir"), ID_PLAY, 86);
        pause_btn = btn(T("&Pausa"), ID_PAUSE, 64);
        btn(T("&Detener"), ID_STOP, 68);
        btn(T("&Abrir..."), ID_OPEN, 74);
        status = add(new Label({0, 0, 10, 10}, ""));
        status->sunken = true;
        status->r = {0, client_h() - 20, client_w(), 20};
        update();
        wm::set_timer(this, 1, 50);
    }

    ~Player() override {
        stop();
        if (snd) api->close(snd);
        mm::kfree(data);
    }

    // Lo que pide la tarjeta (desde la interrupción del reloj)
    static size_t fill(void* ctx, int16_t* out, size_t frames) {
        auto* p = (Player*)ctx;
        size_t n = p->api->read(p->snd, out, frames);
        // copia para el osciloscopio (mezcla de los dos canales)
        size_t from = n > (size_t)SCOPE ? n - SCOPE : 0;
        uint32_t w = p->scope_pos;
        for (size_t i = from; i < n; i++) p->scope[w++ % SCOPE] = (int16_t)((out[i * 2] + out[i * 2 + 1]) / 2);
        p->scope_pos = w % SCOPE;
        return n;
    }

    bool open(const char* pth) {
        uint8_t* d;
        uint32_t size;
        String np = vfs::normalize(pth);
        if (!vfs::read_file(np.c_str(), d, size)) {
            msgbox(this, T("Reproductor de sonido"), String::format(T("No se puede abrir %s.\n%s"), np.c_str(), vfs::error()).c_str(), MB_OK, &icon_error);
            return false;
        }
        SoundInfo ni;
        const char* err = "";
        Sound* ns = api->open(d, size, ni, err);
        if (!ns) {
            mm::kfree(d);
            msgbox(this, T("Reproductor de sonido"), String::format(T("No se puede reproducir %s.\n\n%s"), vfs::basename(np.c_str()).c_str(), err).c_str(),
                   MB_OK, &icon_error);
            return false;
        }
        stop();
        if (snd) api->close(snd);
        mm::kfree(data);
        data = d;
        snd = ns;
        info = ni;
        path = np;
        paused_at = 0;
        seek->maximum = (int)min<uint64_t>(info.frames, 0x7FFFFFFF);
        seek->page = max(1, seek->maximum / 10);
        seek->set(0);
        set_title((vfs::basename(path.c_str()) + T(" - Reproductor de sonido")).c_str());
        update();
        return true;
    }

    uint64_t position() {
        if (!snd) return 0;
        if (!playing) return paused_at;
        uint64_t p = start_frame + audio::frames_played() * info.rate / 48000;
        return min(p, info.frames);
    }

    void play() {
        if (!snd) return;
        if (!audio::available()) {
            msgbox(this, T("Reproductor de sonido"), T("No se ha encontrado ninguna tarjeta de sonido compatible."), MB_OK, &icon_warning);
            return;
        }
        if (playing_now && playing_now != this) playing_now->pause();
        if (paused_at >= info.frames) paused_at = 0;
        api->seek(snd, paused_at);
        start_frame = paused_at;
        if (audio::stream(fill, this)) {
            playing = true;
            playing_now = this;
        }
        update();
    }

    void pause() {
        if (!playing) return;
        paused_at = position();
        stop();
        update();
    }

    void stop() {
        if (playing) {
            audio::stop();
            playing = false;
            if (playing_now == this) playing_now = nullptr;
        }
        for (auto& v : scope) v = 0;
    }

    static String fmt_time(uint64_t frames, uint32_t rate) {
        uint64_t cs = rate ? frames * 100 / rate : 0;   // centésimas
        return String::format(T("%lu,%02lu s"), cs / 100, cs % 100);
    }

    void update() {
        uint64_t p = position();
        pos_lbl->set_text(fmt_time(p, info.rate).c_str());
        len_lbl->set_text(fmt_time(info.frames, info.rate).c_str());
        if (!seek->dragging) seek->set((int)min<uint64_t>(p, 0x7FFFFFFF));
        play_btn->enabled = snd && !playing;
        pause_btn->enabled = playing;
        if (!snd) status->set_text(T("Abra un archivo de sonido (.wav o .mp3) con el botón Abrir."));
        else
            status->set_text(String::format(T("%s   %u Hz, %s, %s"), vfs::basename(path.c_str()).c_str(), info.rate, info.format,
                                            info.channels == 1 ? T("mono") : (info.channels == 2 ? T("estéreo") : T("multicanal"))).c_str());
        invalidate();
    }

    void on_paint(Surface& s) override {
        s.clear(col::FACE);
        // osciloscopio
        Rect o{130, 12, 170, 64};
        s.fill(o, 0x000000);
        s.bevel_thin_sunken(o.inset(-1));
        int mid = o.y + o.h / 2;
        s.hline(o.x, mid, o.w, 0x004000);
        int prev = mid;
        uint32_t start = scope_pos;
        for (int x = 0; x < o.w; x++) {
            int v = scope[(start + x * SCOPE / o.w) % SCOPE];
            int y = mid - v * (o.h / 2 - 2) / 32768;
            if (x) s.line(o.x + x - 1, prev, o.x + x, y, 0x00FF00);
            prev = y;
        }
    }

    void on_timer(int) override {
        if (playing && !audio::playing()) {   // ha terminado
            playing = false;
            if (playing_now == this) playing_now = nullptr;
            paused_at = info.frames;
            for (auto& v : scope) v = 0;
        }
        if (playing || seek->dragging) update();
    }

    void on_command(int id, int code) override {
        switch (id) {
        case ID_START: {
            bool was = playing;
            stop();
            paused_at = 0;
            if (was) play();
            update();
            break;
        }
        case ID_PLAY: play(); break;
        case ID_PAUSE: pause(); break;
        case ID_STOP: stop(); paused_at = 0; update(); break;
        case ID_OPEN: {
            String p = path.empty() ? vfs::docs_dir() : vfs::parent(path.c_str());
            if (file_dialog(this, false, T("Abrir"), "WAV;MP3;MP2", p)) open(p.c_str());
            break;
        }
        case ID_SEEK:
            if (!snd) break;
            {
                bool was = playing;
                stop();
                paused_at = (uint64_t)seek->value;
                if (was) play();
                else update();
            }
            break;
        }
    }

    bool on_key(const InputEvent& e) override {
        if (e.pressed && e.ch == ' ') { playing ? pause() : play(); return true; }
        return false;
    }

    void retranslate() override {
        Window::retranslate();
        if (snd) set_title((vfs::basename(path.c_str()) + T(" - Reproductor de sonido")).c_str());
        update();
    }
};
}

namespace apps {
void player(const char* path) {
    if (!sound()) {
        msgbox(nullptr, T("Reproductor de sonido"),
               T("Falta la biblioteca del sistema sonido.bsl, que es la que lee los sonidos.\n"
                 "Vuelva a instalarla con un paquete de actualización (.bsi)."),
               MB_OK, &icon_error);
        return;
    }
    auto* p = new Player();
    p->center();
    wm::add(p);
    if (path && p->open(path)) p->play();
}

bool sound_file(const char* ext) {
    const SoundApi* api = sound();
    return api && api->can_open(ext);
}
}

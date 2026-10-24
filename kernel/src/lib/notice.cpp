#include "lib/notice.h"

namespace {
struct Notice {
    String title, text;
    bool error = false;
};
Vec<Notice> queue;
}

void notice::post(const char* title, const char* text, bool error) {
    if (queue.size() >= 8) return;   // no se acumulan sin límite
    Notice n;
    n.title = title;
    n.text = text;
    n.error = error;
    queue.push(move_val(n));
}

bool notice::take(String& title, String& text, bool& error) {
    if (queue.empty()) return false;
    title = queue[0].title;
    text = queue[0].text;
    error = queue[0].error;
    queue.erase(0);
    return true;
}

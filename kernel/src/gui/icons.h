#pragma once
#include <stdint.h>
#include "gfx/gfx.h"

struct IconImg {
    int w, h;
    const uint32_t* px; // 0xFF00FF = transparente
};
struct IconPair {
    IconImg big;   // 32x32
    IconImg small; // 16x16
};

void draw_icon(Surface& s, const IconImg& ic, int x, int y);
// Icono con tono de selección (azul) como en el escritorio de Win98
void draw_icon_selected(Surface& s, const IconImg& ic, int x, int y);

extern const IconPair icon_computer, icon_folder, icon_folder_open, icon_mydocs, icon_recycle,
    icon_hdd, icon_file_text, icon_file, icon_file_image, icon_notepad, icon_paint, icon_calc,
    icon_cmd, icon_mine, icon_find, icon_programs, icon_shutdown, icon_run, icon_help,
    icon_settings, icon_app, icon_logo, icon_info, icon_warning, icon_error, icon_question,
    icon_floppy, icon_up_folder, icon_arrow_left, icon_arrow_right, icon_cut, icon_copy, icon_paste,
    icon_delete, icon_views, icon_props, icon_usbdrive, icon_package, icon_sound;

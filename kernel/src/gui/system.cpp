#include "gui/power_ui.h"
#include "kernel.h"
#include "gfx/gfx.h"
#include "gfx/bootscreen.h"
#include "gui/wm.h"
#include "gui/desktop.h"
#include "gui/dialogs.h"
#include "gui/bootsplash.h"
#include "boot/bootinfo.h"
#include "lib/log.h"
#include "lib/i18n.h"
#include "fs/vfs.h"
#include "apps/apps.h"
#include "apps/settings.h"
#include "sys/bsi.h"
#include "gui/wallpaper.h"
#include "proc/process.h"

void system_main() {
    bootscreen_disable();
    wm::init();
    // Una actualización interrumpida (p. ej. por un corte de luz) se termina antes de nada
    bsi::recover();
    // La configuración (idioma, teclado, zona horaria) se aplica antes de crear la interfaz
    bootsplash::loading_step(T("Cargando la configuración..."), 75);
    settings::load();
    // grub.cfg al día: idioma, volumen y una línea por cada biblioteca de C:\BrysOS\BSL
    if (!vfs::system_on_ram()) settings::update_boot_line();
    bootsplash::loading_step(T("Comprobando las carpetas del sistema..."), 85);
    vfs::ensure_defaults();
    process::install_bundled();   // programas incluidos en C:\Archivos de programa
    bootsplash::loading_step(T("Preparando el escritorio..."), 95);
    desktop_init();
    wallpaper::load();
    power_ui::init();   // brillo guardado
    bootsplash::loading_step(T("Listo."), 100);
    klog("Escritorio iniciado\n");
    wm::pump();
    // La presentación con sonido sólo se repite si así se ha configurado
    if (cmdline_has("intro") && !settings::intro_always) settings::set_boot_intro(false);
    if (live_mode()) {
        apps::installer();
    } else if (vfs::system_on_ram()) {
        msgbox(nullptr, "BrysOS",
               T("No se encontró la partición del sistema.\n\n"
                 "La unidad C: se ha creado en memoria: los archivos que guarde se perderán al apagar el equipo."),
               MB_OK, &icon_warning);
    } else if (!settings::credits_hidden) {
        // Créditos y licencias de lo que BrysOS incluye de otros proyectos, hasta que se
        // marque "No volver a mostrar"
        apps::credits(true);
    }
    wm::run();
}

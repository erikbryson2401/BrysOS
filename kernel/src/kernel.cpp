#include "arch/gdt.h"
#include "arch/idt.h"
#include "arch/io.h"
#include "mm/memory.h"
#include "gfx/gfx.h"
#include "gfx/bootscreen.h"
#include "drivers/input.h"
#include "lib/log.h"
#include "lib/string.h"
#include "kernel.h"
#include "drivers/pci.h"
#include "drivers/block.h"
#include "drivers/usb.h"
#include "fs/vfs.h"
#include "drivers/acpi.h"
#include "acpi/power.h"
#include "boot/bootinfo.h"
#include "drivers/audio.h"
#include "gui/bootsplash.h"
#include "lib/i18n.h"
#include "bsl/loader.h"
#include "proc/sched.h"

extern "C" void (*__init_array_start[])();
extern "C" void (*__init_array_end[])();

const char* firmware_name() { return boot_info.uefi ? "UEFI" : "BIOS"; }

void* acpi_rsdp() { return boot_info.rsdp_phys ? (void*)boot_info.rsdp_phys : nullptr; }

bool live_mode() { return cmdline_has("live"); }

extern "C" [[noreturn]] void kmain(uint32_t magic, uint32_t mbi) {
    log_init();
    if (!bootinfo_parse(magic, mbi)) {
        klog("BrysOS debe arrancarse con un cargador Multiboot2 (GRUB o Limine)\n");
        halt_forever();
    }
    bootscreen_init();
    klog("BrysOS %s arrancando (firmware %s, cargador \"%s\")\n", BRYSOS_VERSION, firmware_name(), boot_info.loader);
    klog("Línea de órdenes: %s\n", boot_info.cmdline);
    for (int i = 0; i < boot_info.module_count; i++)
        klog("Módulo %d: \"%s\", %lu bytes\n", i, boot_info.modules[i].name, boot_info.modules[i].size);

    gdt::init();
    idt::init();
    pic::init();
    klog("GDT, IDT y PIC configurados\n");
    mm::pmm_init();
    mm::unmap_identity();
    mm::pat_init();
    mm::heap_init();
    // Constructores globales (pueden reservar memoria del montón)
    for (auto* f = __init_array_start; f != __init_array_end; f++) (*f)();
    pit::init(1000);
    sched::init();   // hilo 0 = el núcleo; SSE para los programas .run
    sti();
    klog("Temporizador PIT a 1000 Hz\n");

    // Bibliotecas del sistema (.bsl) que el cargador de arranque dejó en memoria; así
    // lenguajes.bsl traduce ya los mensajes de la pantalla de carga
    bsl::load_boot_modules();
    i18n::attach();

    // Idioma de los mensajes de arranque (GRUB lo indica con lang=xx)
    char lang[8];
    if (cmdline_get("lang", lang, sizeof lang)) i18n::set_language(i18n::from_code(lang));

    gfx::init();
    pci::init();
    audio::init();
    // volumen guardado (GRUB lo pasa en la línea de órdenes para que valga ya en el logo)
    audio::set_volume(cmdline_int("vol", 80));
    audio::set_mute(cmdline_has("mute"));
    bootscreen_disable();

    // Primer arranque tras la instalación (o si se pidió en el Panel de control):
    // logo con el sonido de inicio
    if (cmdline_has("intro")) bootsplash::intro();

    // Pantalla de carga: cada paso es la inicialización real del hardware
    bootsplash::loading_begin();
    bootsplash::loading_step(T("Iniciando el USB..."), 5);
    usb::init();       // antes que PS/2: el traspaso BIOS->SO desactiva la emulación PS/2
    bootsplash::loading_step(T("Detectando el teclado, el ratón y el touchpad..."), 20);
    ps2::init();
    bootsplash::loading_step(T("Buscando discos..."), 35);
    ahci::init();
    ata::init();
    // Un disco real puede tardar en estar listo: se reintenta durante 30 segundos
    {
        constexpr uint64_t WAIT_MS = 30000;
        uint64_t start = pit::ticks();
        int attempt = 1;
        while (!block::fixed_count() && pit::ticks() - start < WAIT_MS) {
            attempt++;
            char msg[96];
            ksnprintf(msg, sizeof msg, T("Buscando discos... (intento %d)"), attempt);
            bootsplash::loading_step(msg, 35 + (int)((pit::ticks() - start) * 14 / WAIT_MS));
            pit::sleep_ms(1000);
            ahci::rescan();
        }
        if (!block::fixed_count()) klog("No se ha encontrado ningún disco tras %d intentos\n", attempt);
        // Desde el pendrive se puede seguir y probar el sistema (el instalador avisará);
        // un sistema instalado no puede arrancar sin su disco.
        if (!block::fixed_count() && !live_mode()) {
            bootsplash::fatal(T("No se encontró ningún disco duro"),
                              T("BrysOS no ha podido comunicarse con el disco del equipo y no puede continuar.\n\n"
                                "Compruebe que el disco está bien conectado. Si usa una máquina virtual,\n"
                                "conecte el disco a un controlador SATA (AHCI) o IDE.\n\n"
                                "Pulse una tecla para reiniciar el equipo."));
        }
    }
    bootsplash::loading_step(T("Montando las unidades..."), 50);
    vfs::init();
    // las que falten (p. ej. tras una actualización, antes de que GRUB las cargue)
    if (!live_mode()) {
        bootsplash::loading_step(T("Cargando las bibliotecas del sistema..."), 56);
        if (!vfs::system_on_ram()) bsl::restore_missing();
        bsl::load_directory();
    }
    bsl::load_embedded();   // si aún falta alguna, la copia que lleva el núcleo
    i18n::attach();
    bootsplash::loading_step(T("Configurando la administración de energía (ACPI)..."), 62);
    acpi::init();
    power::init();   // AML: batería, cargador y brillo

    system_main();
}

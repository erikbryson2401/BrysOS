# BrysOS - sistema de compilación (ejecutar dentro de WSL)
.SUFFIXES:

# Versión del sistema: make VERSION=1.1 (también la usa el paquete de actualización)
VERSION  ?= 1.3-alfa2
ISO      := BrysOS.iso
KERNEL   := build/kernel.elf
# Prefijo del compilador cruzado: vacío en Linux (g++ del sistema); en Windows (MSYS2),
# compilar.ps1 usa CROSS=x86_64-elf- (E:\BrysOS-dev\cross)
CROSS    ?=
CXX      := $(CROSS)g++
LD       := $(CROSS)ld
STRIP    := $(CROSS)strip
NM       := $(CROSS)nm
NASM     := nasm
# GRUB (módulos y boot.img), Limine y firmware UEFI para QEMU
GRUB_DIR     ?= /usr/lib/grub
GRUB_MKIMAGE ?= grub-mkimage
LIMINE       ?= limine/limine
OVMF         ?= /usr/share/ovmf/OVMF.fd

CXXFLAGS := -std=gnu++20 -O2 -g -pipe -Wall -Wextra -Wno-unused-parameter \
	-ffreestanding -fno-exceptions -fno-rtti -fno-stack-protector -fno-stack-check \
	-fno-lto -fno-PIC -fno-pie -fno-threadsafe-statics -fno-use-cxa-atexit \
	-fno-omit-frame-pointer -fno-strict-aliasing -finput-charset=UTF-8 -fexec-charset=ISO-8859-1 \
	-m64 -march=x86-64 -mabi=sysv -mno-80387 -mno-mmx -mno-sse -mno-sse2 \
	-mno-red-zone -mcmodel=kernel -mgeneral-regs-only \
	-Ikernel/include -Ikernel/src -Ibuild/gen -MMD -MP
LDFLAGS  := -m elf_x86_64 -nostdlib -static -z max-page-size=0x1000 \
	-z noexecstack -T kernel/linker.ld

# Bibliotecas del sistema (.bsl): una carpeta por biblioteca en bsl/, código independiente
# de la posición y enlazado como biblioteca compartida
BSL_CXXFLAGS := $(filter-out -fno-PIC -fno-pie -mcmodel=kernel,$(CXXFLAGS)) -fPIC -fvisibility=hidden
BSL_LDFLAGS  := -m elf_x86_64 -shared -nostdlib -Bsymbolic --hash-style=sysv -z max-page-size=0x1000 \
	-z noexecstack -z text
BSL_NAMES    := $(sort $(notdir $(wildcard bsl/*)))
BSL_FILES    := $(BSL_NAMES:%=build/bsl/%.bsl)

# Programas .run: una carpeta por programa en programas/, en C con el SDK (sdk/)
RUN_CC     := $(CROSS)gcc
RUN_CFLAGS := -std=gnu11 -O2 -g -pipe -Wall -Wextra -Wno-unused-parameter -ffreestanding -fno-pic -fno-pie 	-fno-stack-protector -fno-stack-check -fno-builtin -fno-tree-loop-distribute-patterns 	-finput-charset=UTF-8 -fexec-charset=ISO-8859-1 -m64 -march=x86-64 -Isdk/include -MMD -MP
RUN_LDFLAGS := -m elf_x86_64 -nostdlib -static -z max-page-size=0x1000 -z noexecstack -T sdk/run.ld
RUN_NAMES  := $(sort $(notdir $(wildcard programas/*)))
RUN_FILES  := $(RUN_NAMES:%=build/run/%.run)
RUN_LIB    := build/run/obj/sdk/crt0.o build/run/obj/sdk/librun.o

# Código portado de Linux (en C, GPLv2): kernel/src/linux/. Se compila con las cabeceras de
# compatibilidad de kernel/src/linux/include, que imitan las del núcleo Linux.
LINUX_CFLAGS := $(filter-out -std=gnu++20 -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit \
	-Ikernel/src,$(CXXFLAGS)) -std=gnu11 -Ikernel/src/linux/include -Ikernel/src/linux -include linux/kconfig.h -D__KERNEL__ \
	-fno-common -Wno-sign-compare -Wno-missing-field-initializers -Wno-unused-function -Wno-pointer-sign
CSRC := $(sort $(shell find kernel/src -name '*.c'))

FONT_SRC := kernel/src/gfx/font_data.cpp
ICON_SRC := kernel/src/gui/icon_data.cpp
CXXSRC   := $(sort $(shell find kernel/src -name '*.cpp') $(FONT_SRC) $(ICON_SRC))
ASMSRC   := $(shell find kernel/src -name '*.asm')
OBJ      := $(CXXSRC:kernel/src/%.cpp=build/obj/%.o) $(CSRC:kernel/src/%.c=build/obj/%.c.o) \
	$(ASMSRC:kernel/src/%.asm=build/obj/%.asm.o)

.PHONY: all iso run run-uefi clean disk bsi release programas FORCE

all: $(ISO)

programas: $(RUN_FILES)

# --- .bsl
build/bsl/obj/%.o: bsl/%.cpp | build/gen/version.h
	@mkdir -p $(dir $@)
	$(CXX) $(BSL_CXXFLAGS) -c $< -o $@

# El decodificador MP3 (minimp3) usa coma flotante con SSE; sonido.cpp guarda los registros
# de la FPU y desactiva las interrupciones mientras lo usa.
build/bsl/obj/sonido/mp3.o: BSL_CXXFLAGS := $(filter-out -mno-80387 -mno-mmx -mno-sse -mno-sse2 -mgeneral-regs-only,$(BSL_CXXFLAGS)) 	-msse2 -mfpmath=sse -mstackrealign -w

define BSL_RULE
build/bsl/$(1).bsl: $(patsubst bsl/%.cpp,build/bsl/obj/%.o,$(wildcard bsl/$(1)/*.cpp))
	@mkdir -p build/bsl
	$(LD) $(BSL_LDFLAGS) $$^ -o $$@.full
	$(STRIP) --strip-debug -o $$@ $$@.full
endef
$(foreach n,$(BSL_NAMES),$(eval $(call BSL_RULE,$(n))))

# --- .run
build/run/obj/sdk/crt0.o: sdk/lib/crt0.S
	@mkdir -p $(dir $@)
	$(RUN_CC) -c $< -o $@

build/run/obj/sdk/%.o: sdk/lib/%.c
	@mkdir -p $(dir $@)
	$(RUN_CC) $(RUN_CFLAGS) -c $< -o $@

build/run/obj/%.o: programas/%.c
	@mkdir -p $(dir $@)
	$(RUN_CC) $(RUN_CFLAGS) -c $< -o $@

define RUN_RULE
build/run/$(1).run: $(patsubst programas/%.c,build/run/obj/%.o,$(wildcard programas/$(1)/*.c)) $(RUN_LIB) sdk/run.ld
	$(LD) $(RUN_LDFLAGS) $(RUN_LIB) $$(filter %.o,$$(filter-out $(RUN_LIB),$$^)) -o $$@.full
	$(STRIP) --strip-debug -o $$@ $$@.full
endef
$(foreach n,$(RUN_NAMES),$(eval $(call RUN_RULE,$(n))))

# Programas de prueba (no se instalan): tools/run_pruebas/x.c -> build/run/pruebas/x.run
build/run/pruebas/%.run: tools/run_pruebas/%.c $(RUN_LIB) sdk/run.ld
	@mkdir -p $(dir $@)
	$(RUN_CC) $(RUN_CFLAGS) -c $< -o $@.o
	$(LD) $(RUN_LDFLAGS) $(RUN_LIB) $@.o -o $@

# Los programas van dentro del núcleo y se instalan en C:\Archivos de programa
build/gen/run_embed.asm: $(RUN_FILES) tools/genembed.py
	@mkdir -p $(dir $@)
	python3 tools/genembed.py --symbol run_embedded $(RUN_FILES) > $@

build/gen/run_embed.o: build/gen/run_embed.asm
	$(NASM) -f elf64 $< -o $@

$(FONT_SRC): tools/genfont.py
	python3 tools/genfont.py > $@

$(ICON_SRC): tools/genicons.py BrysOS_logo.png bsi.png
	python3 tools/genicons.py > $@

# Se reescribe sólo si cambia la versión (así no se recompila todo cada vez)
build/gen/version.h: FORCE
	@mkdir -p $(dir $@)
	@echo '#define BRYSOS_VERSION "$(VERSION)"' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@; rm -f $@.tmp

build/obj/%.c.o: kernel/src/%.c | build/gen/version.h
	@mkdir -p $(dir $@)
	$(CROSS)gcc $(LINUX_CFLAGS) -c $< -o $@

build/obj/%.o: kernel/src/%.cpp | build/gen/version.h
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Logo y sonido de presentación
build/assets/intro.pcm: intro.mp3
	@mkdir -p $(dir $@)
	ffmpeg -loglevel error -y -i $< -f s16le -acodec pcm_s16le -ac 2 -ar 48000 $@

build/assets/logo.raw: BrysOS_logo.png tools/genlogo.py
	@mkdir -p $(dir $@)
	python3 tools/genlogo.py $< $@ 320

build/obj/gfx/assets.asm.o: build/assets/intro.pcm build/assets/logo.raw

build/obj/%.asm.o: kernel/src/%.asm
	@mkdir -p $(dir $@)
	$(NASM) -f elf64 -g $< -o $@

# Enlazado en dos pasos: el primero da la lista de símbolos globales del núcleo, con la que
# se genera la tabla de exportaciones que usan las .bsl; el segundo la incluye.
# Copias de reserva de las bibliotecas dentro del núcleo
build/gen/bsl_embed.asm: $(BSL_FILES) tools/genembed.py
	@mkdir -p $(dir $@)
	python3 tools/genembed.py $(BSL_FILES) > $@

build/gen/bsl_embed.o: build/gen/bsl_embed.asm
	$(NASM) -f elf64 $< -o $@

# Créditos y licencias (licencias/NN-nombre.txt), dentro del núcleo en Latin-1
LIC_FILES := $(patsubst licencias/%,build/licencias/%,$(sort $(wildcard licencias/[0-9]*.txt)))
build/licencias/%.txt: licencias/%.txt tools/genlicenses.py
	@mkdir -p $(dir $@)
	python3 tools/genlicenses.py $< $@

build/gen/lic_embed.asm: $(LIC_FILES) tools/genembed.py
	@mkdir -p $(dir $@)
	python3 tools/genembed.py --symbol licenses_embedded $(LIC_FILES) > $@

build/gen/lic_embed.o: build/gen/lic_embed.asm
	$(NASM) -f elf64 $< -o $@

build/gen/bsl_exports_stub.o: tools/genexports.py
	@mkdir -p $(dir $@)
	python3 tools/genexports.py --stub > build/gen/bsl_exports_stub.S
	$(CXX) -c build/gen/bsl_exports_stub.S -o $@

build/kernel-stage1.elf: $(OBJ) build/gen/bsl_embed.o build/gen/run_embed.o build/gen/lic_embed.o build/gen/bsl_exports_stub.o kernel/linker.ld
	$(LD) $(LDFLAGS) $(OBJ) build/gen/bsl_embed.o build/gen/run_embed.o build/gen/lic_embed.o build/gen/bsl_exports_stub.o -o $@

build/gen/bsl_exports.o: build/kernel-stage1.elf tools/genexports.py
	$(NM) -g --defined-only $< | python3 tools/genexports.py > build/gen/bsl_exports.S
	$(CXX) -c build/gen/bsl_exports.S -o $@

$(KERNEL): $(OBJ) build/gen/bsl_embed.o build/gen/run_embed.o build/gen/lic_embed.o build/gen/bsl_exports.o kernel/linker.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) $(OBJ) build/gen/bsl_embed.o build/gen/run_embed.o build/gen/lic_embed.o build/gen/bsl_exports.o -o $@

limine/limine:
	$(MAKE) -C limine

# GRUB para el disco instalado (BIOS y UEFI), con todos los módulos dentro
GRUB_COMMON := part_msdos part_gpt fat ntfs ext2 search search_fs_file search_fs_uuid search_label 	configfile normal multiboot2 chain echo test ls cat reboot halt sleep minicmd video video_fb 	gfxterm font
GRUB_BIOS := biosdisk vbe ntldr $(GRUB_COMMON)
GRUB_EFI := efi_gop $(GRUB_COMMON)

build/grub/core.img: tools/grub-early.cfg
	@mkdir -p $(dir $@)
	$(GRUB_MKIMAGE) -d $(GRUB_DIR)/i386-pc -O i386-pc -o $@ -p /brysos -c tools/grub-early.cfg $(GRUB_BIOS)

build/grub/grubx64.efi: tools/grub-early.cfg
	@mkdir -p $(dir $@)
	$(GRUB_MKIMAGE) -d $(GRUB_DIR)/x86_64-efi -O x86_64-efi -o $@ -p /brysos -c tools/grub-early.cfg $(GRUB_EFI)

build/grub/boot.img:
	@mkdir -p $(dir $@)
	cp $(GRUB_DIR)/i386-pc/boot.img $@

GRUB_FILES := build/grub/boot.img build/grub/core.img build/grub/grubx64.efi

$(ISO): $(KERNEL) $(BSL_FILES) limine.conf $(LIMINE) $(GRUB_FILES)
	rm -rf build/iso_root
	mkdir -p build/iso_root/boot/limine build/iso_root/boot/grub build/iso_root/boot/bsl build/iso_root/EFI/BOOT
	cp $(BSL_FILES) build/iso_root/boot/bsl/
	cp $(KERNEL) build/iso_root/boot/kernel.elf
	$(STRIP) --strip-debug build/iso_root/boot/kernel.elf
	cp $(GRUB_FILES) build/iso_root/boot/grub/
	cp limine/limine-bios.sys limine/limine-bios-cd.bin limine/limine-uefi-cd.bin build/iso_root/boot/limine/
	cp limine.conf build/iso_root/boot/limine/limine.conf
	for f in $(BSL_NAMES); do printf '    module_path: boot():/boot/bsl/%s.bsl\n    module_string: %s.bsl\n' $$f $$f \
		>> build/iso_root/boot/limine/limine.conf; done
	cp limine/BOOTX64.EFI limine/BOOTIA32.EFI build/iso_root/EFI/BOOT/
	xorriso -as mkisofs -R -r -J -b boot/limine/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table -hfsplus \
		-apm-block-size 2048 --efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		build/iso_root -o $(ISO) 2>/dev/null
	./$(LIMINE) bios-install $(ISO) 2>/dev/null

# Paquete de actualización con el núcleo actual: make bsi VERSION=1.1
# Las notas de la versión (si existe el archivo) se muestran en el Actualizador.
# Cada versión publicada se guarda en iso/ y bsi/ (make release VERSION=x.y)
BSI       := bsi/BrysOS-$(VERSION).bsi
BSI_NOTES ?= notas-$(VERSION).txt
BSI_EXTRA ?=
# BSI_LIBS=0: sólo el núcleo (para actualizar desde la 1.0, cuyo actualizador estropea los
# archivos si el paquete trae varios; el núcleo nuevo instala solo sus bibliotecas).
# Con las bibliotecas, el paquete exige la 1.1 o posterior.
BSI_LIBS  ?= 1
BSI_CONTENT := build/kernel-bsi.elf:kernel.elf $(if $(filter 1,$(BSI_LIBS)),$(foreach n,$(BSL_NAMES),build/bsl/$(n).bsl:bsl/$(n).bsl) --min-version 1.1)
bsi: $(BSI)
$(BSI): $(KERNEL) $(BSL_FILES) tools/mkbsi.py FORCE
	@mkdir -p bsi
	$(STRIP) --strip-debug -o build/kernel-bsi.elf $(KERNEL)
	python3 tools/mkbsi.py -o $@ --version $(VERSION) --name "BrysOS $(subst -, ,$(VERSION))" --reboot \
		$(if $(wildcard $(BSI_NOTES)),--desc-file $(BSI_NOTES)) \
		$(BSI_CONTENT) $(BSI_EXTRA)

# Disco de datos: 256 MiB, MBR con una partición FAT32 etiquetada BRYSOS
disk: build/disk.img
build/disk.img:
	@mkdir -p build
	dd if=/dev/zero of=$@ bs=1M count=256 status=none
	printf 'label: dos\n2048,,c\n' | sfdisk -q $@
	mkfs.fat -F 32 -n BRYSOS --offset 2048 $@ 260096 >/dev/null

run: $(ISO) build/disk.img
	qemu-system-x86_64 -M q35 -m 512M -cdrom $(ISO) -boot d \
		-drive file=build/disk.img,format=raw,if=none,id=d0 -device ahci,id=ahci -device ide-hd,drive=d0,bus=ahci.0 \
		-serial stdio

run-uefi: $(ISO) build/disk.img
	qemu-system-x86_64 -M q35 -m 512M -drive if=pflash,format=raw,readonly=on,file=$(OVMF) -cdrom $(ISO) -boot d \
		-drive file=build/disk.img,format=raw,if=none,id=d0 -device ahci,id=ahci -device ide-hd,drive=d0,bus=ahci.0 \
		-serial stdio

clean:
	rm -rf build $(ISO)

-include $(OBJ:.o=.d)
-include $(shell find build/bsl/obj build/run/obj -name "*.d" 2>/dev/null)

# Versión publicada: iso/BrysOS-x.y.iso y bsi/BrysOS-x.y.bsi
release: $(ISO) bsi
	@mkdir -p iso
	cp $(ISO) iso/BrysOS-$(VERSION).iso
	@echo "Versión $(VERSION): iso/BrysOS-$(VERSION).iso y $(BSI)"

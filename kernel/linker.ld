OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start32)

KVIRT = 0xffffffff80000000;

PHDRS
{
    boot PT_LOAD;
    text PT_LOAD;
    rodata PT_LOAD;
    data PT_LOAD;
}

SECTIONS
{
    /* Código de arranque de 32 bits: dirección virtual = física */
    . = 0x200000;
    __kernel_phys_start = .;

    .boot : {
        KEEP(*(.multiboot))
        *(.boot32)
    } :boot

    .bootbss : {
        *(.bootbss)
    } :boot

    /* Resto del núcleo en la mitad superior */
    . = ALIGN(4096);
    . += KVIRT;

    .text : AT(ADDR(.text) - KVIRT) {
        *(.text .text.*)
    } :text

    . = ALIGN(4096);

    .rodata : AT(ADDR(.rodata) - KVIRT) {
        *(.rodata .rodata.*)
    } :rodata

    .init_array : AT(ADDR(.init_array) - KVIRT) {
        __init_array_start = .;
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
        KEEP(*(.init_array .ctors))
        __init_array_end = .;
    } :rodata

    . = ALIGN(4096);

    .data : AT(ADDR(.data) - KVIRT) {
        *(.data .data.*)
    } :data

    .bss : AT(ADDR(.bss) - KVIRT) {
        *(.bss .bss.*)
        *(COMMON)
    } :data

    . = ALIGN(4096);
    __kernel_end = .;
    __kernel_phys_end = . - KVIRT;

    /DISCARD/ : {
        *(.eh_frame*)
        *(.note .note.*)
    }
}

; Arranque Multiboot2: el cargador (GRUB o Limine) entra aquí en modo protegido
; de 32 bits. Se construyen las tablas de páginas, se activa el modo largo y se
; salta al núcleo en la mitad superior.
;
; Mapas iniciales (páginas de 2 MiB):
;   0 .. 4 GiB                       identidad (sólo para la transición)
;   0xFFFF800000000000 + 0 .. 4 GiB  mapa directo de la memoria física (HHDM)
;   0xFFFFFFFF80000000 + 0 .. 1 GiB  imagen del núcleo
bits 32

MB2_MAGIC     equ 0xE85250D6
MB2_ARCH      equ 0
KVIRT         equ 0xFFFFFFFF80000000

section .multiboot
align 8
mb2_header:
    dd MB2_MAGIC
    dd MB2_ARCH
    dd mb2_header_end - mb2_header
    dd -(MB2_MAGIC + MB2_ARCH + (mb2_header_end - mb2_header))
    ; Etiqueta de framebuffer (opcional): sin preferencia de tamaño, 32 bpp
    align 8
    dw 5, 1
    dd 20
    dd 0, 0, 32
    ; Módulos alineados a página
    align 8
    dw 6, 0
    dd 8
    ; Fin
    align 8
    dw 0, 0
    dd 8
mb2_header_end:

section .boot32 progbits alloc exec write
global _start32
_start32:
    cli
    cld
    mov esp, boot_stack_top
    mov [mb_magic], eax
    mov [mb_info], ebx

    ; ¿Admite el procesador modo largo?
    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb .no_long_mode
    mov eax, 0x80000001
    cpuid
    test edx, 1 << 29
    jz .no_long_mode
    mov [nx_supported], edx

    ; Tablas de páginas a cero
    mov edi, pml4
    mov ecx, (page_tables_end - pml4) / 4
    xor eax, eax
    rep stosd

    ; 4 directorios de páginas que cubren 0..4 GiB con páginas de 2 MiB
    mov edi, pd0
    mov eax, 0x83            ; presente | escritura | página grande
    xor edx, edx
    mov ecx, 2048
.fill_pd:
    mov [edi], eax
    mov [edi + 4], edx
    add eax, 0x200000
    adc edx, 0
    add edi, 8
    loop .fill_pd

    ; PDPT de la parte baja / HHDM -> pd0..pd3
    mov eax, pd0
    or eax, 3
    mov [pdpt_low], eax
    add eax, 4096
    mov [pdpt_low + 8], eax
    add eax, 4096
    mov [pdpt_low + 16], eax
    add eax, 4096
    mov [pdpt_low + 24], eax

    ; PDPT alto: entrada 510 (0xFFFFFFFF80000000) -> pd0 (primer GiB)
    mov eax, pd0
    or eax, 3
    mov [pdpt_high + 510 * 8], eax

    ; PML4: 0 (identidad), 256 (HHDM) y 511 (núcleo)
    mov eax, pdpt_low
    or eax, 3
    mov [pml4], eax
    mov [pml4 + 256 * 8], eax
    mov eax, pdpt_high
    or eax, 3
    mov [pml4 + 511 * 8], eax

    mov eax, pml4
    mov cr3, eax

    mov eax, cr4
    or eax, 1 << 5           ; PAE
    mov cr4, eax

    mov ecx, 0xC0000080      ; EFER
    rdmsr
    or eax, 1 << 8           ; LME
    test dword [nx_supported], 1 << 20
    jz .no_nx
    or eax, 1 << 11          ; NXE
.no_nx:
    wrmsr

    mov eax, cr0
    or eax, (1 << 31) | (1 << 16) | 1   ; PG | WP | PE
    mov cr0, eax

    lgdt [gdt64_ptr]
    jmp 0x08:long_mode_entry

.no_long_mode:
    ; Sin modo largo: mensaje en modo texto VGA (si existe) y parada
    mov esi, msg_no64
    mov edi, 0xB8000
.msg:
    lodsb
    test al, al
    jz .halt
    mov ah, 0x4F
    stosw
    jmp .msg
.halt:
    hlt
    jmp .halt

bits 64
long_mode_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    xor ax, ax
    mov fs, ax
    mov gs, ax
    mov rsp, kernel_stack_top
    xor rbp, rbp
    mov edi, [mb_magic]
    mov esi, [mb_info]
    mov rax, kmain
    call rax
.hang:
    cli
    hlt
    jmp .hang

section .boot32
align 16
gdt64:
    dq 0
    dq 0x00AF9A000000FFFF
    dq 0x00CF92000000FFFF
gdt64_end:
gdt64_ptr:
    dw gdt64_end - gdt64 - 1
    dq gdt64

msg_no64: db "BrysOS necesita un procesador de 64 bits (x86-64).", 0

align 4
mb_magic: dd 0
mb_info: dd 0
nx_supported: dd 0

section .bootbss nobits alloc write
alignb 4096
pml4:      resb 4096
pdpt_low:  resb 4096
pdpt_high: resb 4096
pd0:       resb 4096 * 4
page_tables_end:
alignb 16
boot_stack: resb 16384
boot_stack_top:

section .bss
alignb 16
kernel_stack: resb 65536
kernel_stack_top:

extern kmain
global boot_pml4
boot_pml4 equ pml4

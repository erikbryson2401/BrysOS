# Punto de entrada de los programas .run: rdi = línea de órdenes
    .section .text
    .global _start
_start:
    xor %rbp, %rbp
    and $-16, %rsp
    call __run_iniciar
    call main
    mov %eax, %edi
    call salir
    ud2
    .section .note.GNU-stack,"",@progbits

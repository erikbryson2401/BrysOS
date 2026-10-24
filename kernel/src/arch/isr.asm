; Stubs de interrupción: guardan el estado y llaman a isr_dispatch(Frame*)
bits 64
section .text
extern isr_dispatch

%macro ISR_NOERR 1
isr_stub_%1:
    push qword 0
    push qword %1
    jmp isr_common
%endmacro

%macro ISR_ERR 1
isr_stub_%1:
    push qword %1
    jmp isr_common
%endmacro

isr_common:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    cld
    mov rdi, rsp
    and rsp, -16
    call isr_dispatch
    mov rsp, rax             ; contexto que devuelve el despachador (otro hilo si cambia)
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    add rsp, 16
    iretq

; Excepciones con código de error: 8, 10-14, 17, 21, 29, 30
%assign i 0
%rep 256
%if i == 8 || (i >= 10 && i <= 14) || i == 17 || i == 21 || i == 29 || i == 30
    ISR_ERR i
%else
    ISR_NOERR i
%endif
%assign i i+1
%endrep

section .rodata
global isr_stub_table
isr_stub_table:
%assign i 0
%rep 256
    dq isr_stub_ %+ i
%assign i i+1
%endrep

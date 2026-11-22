global _start
extern main

section .text
_start:
    call main
    mov  ebx, eax
    mov  eax, 10        ; SYSCALL_EXIT
    int  0x30
    jmp  $              ; safety fallback

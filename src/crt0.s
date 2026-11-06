global _start
extern main

section .text
_start:
    call main

    ; Exit syscall
    ; EAX = nomor syscall EXIT (12)
    ; EBX = return value dari main()
    mov ebx, eax
    mov eax, 12
    int 0x30

.hang:
    jmp .hang
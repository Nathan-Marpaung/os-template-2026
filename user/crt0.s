global _start
extern main

section .text
_start:
    mov eax, [esp]       ; argc
    mov ebx, [esp + 4]   ; argv (char **)
    push ebx             ; argv
    push eax             ; argc
    call main
    add esp, 8

    ; Terminate process via SYS_EXIT (8) when main() returns
    mov ebx, eax
    mov eax, 8
    int 0x30
.hang:
    jmp .hang

global _start
extern main

section .text
_start:
    ; Stack yang disiapkan kernel (SYSCALL_EXEC_ARGS):
    ; ESP              -> argc
    ; ESP+4            -> argv[0]
    ; ESP+8            -> argv[1]
    ; ...
    ; ESP+4*(argc+1)   -> NULL
    ; ESP+4*(argc+2)   -> inode cwd shell (tambahan priasolOS)

    ; Base pointer
    mov  ebp, esp

    ; Clear registers
    xor  ebx, ebx
    xor  ecx, ecx
    xor  edx, edx
    xor  esi, esi
    xor  edi, edi

    ; Call main function: main(argc, argv)
    mov  eax, [esp]          ; argc
    lea  ecx, [esp + 4]      ; argv
    push ecx
    push eax
    call main

    ; Exit program (terminate process), exit code = return value main
    mov  ebx, eax
    mov  eax, 10             ; SYSCALL_EXIT
    int  0x30

    ; Tidak akan sampai sini. Jangan pakai cli/hlt: keduanya instruksi ring 0
    jmp  $
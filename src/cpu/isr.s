global isr_default_handler
global isr_keyboard_handler
global isr_timer_handler
global isr_syscall_handler
global isr_page_fault_handler

extern keyboard_handler
extern pic_send_eoi
extern syscall_handler
extern page_fault_handler
extern kernel_return_from_user

section .text

isr_default_handler:
    pusha
    popa
    iret

isr_timer_handler:
    pusha
    push dword 0
    call pic_send_eoi
    add esp, 4
    popa
    iret

isr_keyboard_handler:
    pusha

    ; Simpan segment register user
    push ds
    push es
    push fs
    push gs

    ; Gunakan kernel data segment
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    call keyboard_handler

    ; Pulihkan segment register user
    pop gs
    pop fs
    pop es
    pop ds

    popa
    iret

; System call (int 0x30), dipanggil user program dari ring 3.
; Argumen diteruskan ke syscall_handler(eax, ebx, ecx, edx) lewat stack (cdecl).
; Semua register dikembalikan seperti semula dengan popa.
isr_syscall_handler:
    pusha

    ; Simpan segment register milik user
    push ds
    push es
    push fs
    push gs

    ; Gunakan kernel data segment selama menjalankan C kernel
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; Pulihkan eax asli karena mov ax, 0x10 mengubah eax
    mov eax, [esp + 44]

    ; Teruskan argumen syscall (cdecl)
    push edx
    push ecx
    push ebx
    push eax

    call syscall_handler
    add esp, 16

    ; Jika syscall_handler mengembalikan 1, berarti EXIT
    cmp eax, 1
    je kernel_return_from_user

    ; Pulihkan segment register user
    pop gs
    pop fs
    pop es
    pop ds

    ; Pulihkan register dan kembali ke user
    popa
    iret

; Exception 14 (Page Fault)
isr_page_fault_handler:
    pusha
    mov eax, cr2
    push dword [esp + 32]
    push eax
    call page_fault_handler
    add esp, 8
    popa
    add esp, 4
    iret
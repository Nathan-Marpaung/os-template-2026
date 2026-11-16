extern main_interrupt_handler
global call_generic_handler
global process_context_switch

KERNEL_VIRTUAL_ADDRESS_BASE equ 0xC0000000
USER_CODE_SEGMENT_SELECTOR  equ 0x1B
USER_DATA_SEGMENT_SELECTOR  equ 0x23

section .text
call_generic_handler:
    ; At entry, the interrupt stub and CPU frame are already on the stack.
    ; A privilege transition additionally places user ESP and SS at +20/+24.
    push ds
    push es
    push fs
    push gs
    pushad

    push eax
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    pop eax

    mov eax, esp
    push eax
    call main_interrupt_handler
    add esp, 4

    popad
    pop gs
    pop fs
    pop es
    pop ds
    add esp, 8
    iretd

process_context_switch:
    ; Context is passed by value. Keep its address in ebp while loading CR3.
    mov ebp, esp
    add ebp, 4

    mov eax, [ebp + 56]
    cmp eax, KERNEL_VIRTUAL_ADDRESS_BASE
    jb .load_page_directory
    sub eax, KERNEL_VIRTUAL_ADDRESS_BASE
.load_page_directory:
    mov cr3, eax

    push dword [ebp + 44]
    push dword [ebp + 40]
    push dword [ebp + 36]
    push dword [ebp + 32]
    pop gs
    pop fs
    pop es
    pop ds

    push dword USER_DATA_SEGMENT_SELECTOR
    push dword [ebp + 12]
    push dword [ebp + 52]
    push dword USER_CODE_SEGMENT_SELECTOR
    push dword [ebp + 48]

    push dword [ebp + 28]
    push dword [ebp + 24]
    push dword [ebp + 20]
    push dword [ebp + 16]
    push dword [ebp + 12]
    push dword [ebp + 8]
    push dword [ebp + 4]
    push dword [ebp + 0]
    popad
    iretd

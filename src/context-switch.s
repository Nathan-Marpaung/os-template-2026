global process_context_switch

KERNEL_VIRTUAL_ADDRESS_BASE equ 0xC0000000
USER_CODE_SEGMENT_SELECTOR  equ 0x1B
USER_DATA_SEGMENT_SELECTOR  equ 0x23

section .text
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

    mov ax, [ebp + 44]
    mov ds, ax
    mov ax, [ebp + 40]
    mov es, ax
    mov ax, [ebp + 36]
    mov fs, ax
    mov ax, [ebp + 32]
    mov gs, ax

    push dword USER_DATA_SEGMENT_SELECTOR
    push dword [ebp + 12]
    push dword [ebp + 52]
    push dword USER_CODE_SEGMENT_SELECTOR
    push dword [ebp + 48]

    mov edi, [ebp + 0]
    mov esi, [ebp + 4]
    mov ebx, [ebp + 16]
    mov edx, [ebp + 20]
    mov ecx, [ebp + 24]
    mov eax, [ebp + 28]
    mov ebp, [ebp + 8]
    iretd

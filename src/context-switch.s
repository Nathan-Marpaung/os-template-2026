global process_context_switch

; Offset setiap field di struct Context (lihat header/process/process.h)
; struct Context __attribute__((packed)) {
;     struct CPURegister cpu;        ; +0
;         index.edi                  ; +0
;         index.esi                  ; +4
;         stack.ebp                  ; +8
;         stack.esp                  ; +12
;         general.ebx                ; +16
;         general.edx                ; +20
;         general.ecx                ; +24
;         general.eax                ; +28
;         segment.gs                 ; +32
;         segment.fs                 ; +36
;         segment.es                 ; +40
;         segment.ds                 ; +44
;     uint32_t eip;                  ; +48
;     uint32_t eflags;               ; +52
;     struct PageDirectory *page_directory_virtual_addr; ; +56 (CR3 diganti di C)
; }

section .text
; void process_context_switch(struct Context ctx)
; ctx dikirim by value, jadi seluruh isi struct ada di stack mulai [esp+4]
process_context_switch:
    ; 1. Simpan base address argument ctx sebelum stack berubah karena push
    lea  ecx, [esp+4]

    ; 2. Siapkan stack untuk iret inter-privilege (kernel ring 0 -> user ring 3)
    ;    Urutan pop oleh iret: eip, cs, eflags, esp, ss
    mov  eax, 0x20 | 0x3        ; GDT_USER_DATA_SEGMENT_SELECTOR, RPL 3
    push eax                    ; ss
    push dword [ecx+12]         ; esp user (ctx.cpu.stack.esp)
    push dword [ecx+52]         ; eflags   (ctx.eflags)
    mov  eax, 0x18 | 0x3        ; GDT_USER_CODE_SEGMENT_SELECTOR, RPL 3
    push eax                    ; cs
    push dword [ecx+48]         ; eip      (ctx.eip)

    ; 3. Load semua register dari ctx
    ;    Segment register dulu (pakai eax sebagai perantara)
    mov  eax, [ecx+32]
    mov  gs, ax
    mov  eax, [ecx+36]
    mov  fs, ax
    mov  eax, [ecx+40]
    mov  es, ax
    mov  eax, [ecx+44]
    mov  ds, ax

    ;    Index, stack (ebp), dan general purpose register
    ;    esp tidak di-load di sini karena esp user diambil iret dari stack
    mov  edi, [ecx+0]
    mov  esi, [ecx+4]
    mov  ebp, [ecx+8]
    mov  ebx, [ecx+16]
    mov  edx, [ecx+20]
    mov  eax, [ecx+28]

    ; 4. Cleanup: ecx di-load paling akhir karena ecx masih dipakai sebagai base address ctx
    mov  ecx, [ecx+24]

    ; 5. Lompat ke process
    iret

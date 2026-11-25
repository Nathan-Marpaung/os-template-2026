ENTRY(_start)

SECTIONS {
    . = 0x00000000; /* Assuming OS will load this program at virtual address */
    
    .text ALIGN(4): {
        [a-z\/]*crt0.o(.text)  /* Put the entrypoint in the front of executable */
        *(.text)
    }

    .data ALIGN(4): {
        *(.data)
    }

    .rodata ALIGN(4): {
        *(.rodata*)
    }

    /* .bss diletakkan paling akhir supaya tidak ikut ditulis ke flat binary (hanya berisi nol).
     * Kernel selalu mengisi nol page baru saat membuat proses, jadi variabel static tetap 0.
     * Ini membuat file shell jauh lebih kecil dari batas ukuran file EXT2 (71.680 byte). */
    .bss ALIGN(4): { /* 9 May Edit: Fixes for static storage variables */
        *(COMMON)
        *(.bss)
    }
    _linker_user_program_end = .;
    ASSERT ((_linker_user_program_end <= 1 * 1024 * 1024), "Error: User program linking result is more than 1 MiB")
}

ENTRY(_start)

SECTIONS {
    . = 0x0;

    .text : ALIGN(4K)
    {
        *crt0-cmd.o(.text)   /* _start harus di alamat 0, kernel mulai eksekusi dari awal file */
        *(.text)
        *(.rodata*)
    }

    .data : ALIGN(4K)
    {
        *(.data)
    }

    .bss : ALIGN(4K)
    {
        *(COMMON)
        *(.bss)
    }

    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }

    _linker_cmd_end = .;
    ASSERT((_linker_cmd_end <= 0x40000), "Error: command lebih dari 256 KiB (buffer_size SYSCALL_EXEC_ARGS)")
}
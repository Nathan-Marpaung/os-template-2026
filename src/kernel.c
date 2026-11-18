#include <stdbool.h>
#include <stdint.h>
#include "header/cpu/gdt.h"
#include "header/cpu/idt.h"
#include "header/cpu/interrupt.h"
#include "header/driver/keyboard.h"
#include "header/filesystem/ext2.h"
#include "header/kernel-entrypoint.h"
#include "header/memory/paging.h"
#include "header/process/process.h"
#include "header/stdlib/string.h"
#include "header/text/framebuffer.h"

// Init process: the shell, a flat binary linked at virtual address 0 (see user-linker.ld)
#define INIT_PROCESS_NAME         "shell"
#define INIT_PROCESS_NAME_LENGTH  5
#define INIT_PROCESS_PARENT_INODE 1u    // root directory inode of this EXT2

static void kernel_print(const char *s, uint8_t color) {
    uint32_t len = 0;
    while (s[len])
        len++;
    framebuffer_puts(s, len, color);
}

void kernel_setup(void)
{
    load_gdt(&_gdt_gdtr);
    pic_remap();
    initialize_idt();
    activate_keyboard_interrupt();
    framebuffer_reset_terminal();
    initialize_filesystem_ext2();

    // Ring 3 support: kernel stack used on every user -> kernel transition
    gdt_install_tss();
    set_tss_register();

    // Create the init process (PID 0). Its binary is loaded into a dedicated
    // page directory by process_create_user_process().
    struct EXT2DriverRequest request = {
        .buf          = (void *) 0,
        .name         = INIT_PROCESS_NAME,
        .name_len     = INIT_PROCESS_NAME_LENGTH,
        .parent_inode = INIT_PROCESS_PARENT_INODE,
        .buffer_size  = PROCESS_BINARY_MAX_SIZE,
        .is_directory = false,
    };
    if (process_create_user_process(request) != PROCESS_CREATE_SUCCESS) {
        kernel_print("kernel: failed to start /shell, run `make insert-shell` first\n", 0xC);
        while (true)
            __asm__ volatile("hlt");
    }

    struct ProcessControlBlock *init_process = process_get_pcb_by_pid(0);
    init_process->metadata.state = PROCESS_STATE_RUNNING;

    set_tss_kernel_current_stack();
    keyboard_state_activate();

    // One way jump into the first process. The scheduler will take over when C merges.
    kernel_execute_user_program(
        (void *) init_process->context.eip,
        init_process->context.page_directory_virtual_addr
    );

    // Unreachable
    while (true)
        __asm__ volatile("hlt");
}

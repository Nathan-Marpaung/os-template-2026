#include <stdint.h>
#include <stdbool.h>
#include "header/cpu/gdt.h"
#include "header/cpu/idt.h"
#include "header/cpu/interrupt.h"
#include "header/driver/keyboard.h"
#include "header/text/framebuffer.h"
#include "header/kernel-entrypoint.h"
#include "header/driver/disk.h"
#include "header/filesystem/ext2.h"
#include "header/memory/paging.h"
#include "header/process/process.h"
#include "header/scheduler/scheduler.h"

void kernel_setup(void) {
    // Basic Kernel Initializations
    load_gdt(&_gdt_gdtr);
    pic_remap();
    initialize_idt();
    activate_keyboard_interrupt();
    framebuffer_clear();
    framebuffer_set_cursor(0, 0);

    // Filesystem & TSS Initializations
    initialize_filesystem_ext2();
    gdt_install_tss();
    set_tss_register();

    _paging_kernel_page_directory.table[RECURSIVE_PAGE_DIRECTORY_INDEX] = (struct PageDirectoryEntry) {
    .flag = { .present_bit = 1, .write_bit = 1 },
    .page_table_base_address =
        ((uint32_t) &_paging_kernel_page_directory - KERNEL_VIRTUAL_ADDRESS_BASE) >> 12,
};

    struct EXT2DriverRequest request = {
        .buf = (uint8_t*) 0,
        .name = "shell",
        .parent_inode = 1,  
        .buffer_size = 0x100000,
        .name_len = 5,
    };
    
    set_tss_kernel_current_stack();
    int32_t pr = process_create_user_process(request);   // .buf=0, .name="shell", .parent_inode=1, .buffer_size=0x100000, .name_len=5
    if (pr != PROCESS_CREATE_SUCCESS) {
        framebuffer_write(0, 0, 'P', 0xC, 0);
        framebuffer_write(0, 1, '0' + pr, 0xC, 0);       // 1=max procs, 2=bad entry, 3=memory, 4=fs read
        while (true);
    }

    scheduler_init();
    scheduler_switch_to_next_process();
}
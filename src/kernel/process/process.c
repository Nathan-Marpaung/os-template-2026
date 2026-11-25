#include "header/process/process.h"
#include "header/memory/paging.h"
#include "header/stdlib/string.h"
#include "header/cpu/gdt.h"

struct ProcessControlBlock _process_list[PROCESS_COUNT_MAX];
struct ProcessManagerState process_manager_state = { .active_process_count = 0 };
static uint32_t next_pid = 1;

static uint32_t ceil_div(uint32_t a, uint32_t b) {
    return (a + b - 1) / b;
}

static int32_t process_list_get_inactive_index(void) {
    for (uint32_t i = 0; i < PROCESS_COUNT_MAX; i++)
        if (!_process_list[i].metadata.active)
            return (int32_t) i;
    return -1;
}

static uint32_t process_generate_new_pid(void) {
    return next_pid++;
}


int32_t process_create_user_process(struct EXT2DriverRequest request) {
    int32_t retcode = PROCESS_CREATE_SUCCESS;
    struct PageDirectory *prev_dir = paging_get_current_page_directory_addr();
    struct PageDirectory *new_dir  = NULL;
    struct ProcessControlBlock *new_pcb = NULL;

    if (process_manager_state.active_process_count >= PROCESS_COUNT_MAX) {
        retcode = PROCESS_CREATE_FAIL_MAX_PROCESS_EXCEEDED;
        goto exit_cleanup;
    }

    // Ensure entrypoint is not located at kernel's section at higher half
    if ((uint32_t) request.buf >= KERNEL_VIRTUAL_ADDRESS_BASE) {
        retcode = PROCESS_CREATE_FAIL_INVALID_ENTRYPOINT;
        goto exit_cleanup;
    }

    // Executable pages + user stack pages
    uint32_t code_pages = ceil_div(request.buffer_size, PAGE_FRAME_SIZE);
    uint32_t page_frame_count_needed = code_pages + PROCESS_STACK_PAGE_COUNT;
    if (page_frame_count_needed > PROCESS_PAGE_FRAME_COUNT_MAX ||
        !paging_allocate_check(page_frame_count_needed * PAGE_FRAME_SIZE)) {
        retcode = PROCESS_CREATE_FAIL_NOT_ENOUGH_MEMORY;
        goto exit_cleanup;
    }

    // Process PCB
    int32_t p_index = process_list_get_inactive_index();
    if (p_index < 0) {
        retcode = PROCESS_CREATE_FAIL_MAX_PROCESS_EXCEEDED;
        goto exit_cleanup;
    }
    new_pcb = &(_process_list[p_index]);
    memset(new_pcb, 0, sizeof(*new_pcb));
    new_pcb->metadata.pid = process_generate_new_pid();

    // New virtual address space; switch into it before allocating so every page is zeroed
    new_dir = paging_create_new_page_directory();
    if (new_dir == NULL) {
        retcode = PROCESS_CREATE_FAIL_NOT_ENOUGH_MEMORY;
        goto exit_cleanup;
    }
    paging_use_page_directory(new_dir);

    for (uint32_t i = 0; i < page_frame_count_needed; i++) {
        void *va = (i < code_pages)
            ? (void *) ((uint32_t) request.buf + i * PAGE_FRAME_SIZE)
            : (void *) (PROCESS_STACK_TOP - (i - code_pages + 1) * PAGE_FRAME_SIZE);
        if (!paging_allocate_user_page_frame(new_dir, va)) {
            retcode = PROCESS_CREATE_FAIL_NOT_ENOUGH_MEMORY;
            goto exit_cleanup;
        }
        new_pcb->memory.virtual_addr_used[new_pcb->memory.page_frame_used_count++] = va;
    }

    // Load the executable; new_dir is active, so request.buf is a virtual address in the new space
    if (read(request) != 0) {
        retcode = PROCESS_CREATE_FAIL_FS_READ_FAILURE;
        goto exit_cleanup;
    }

    // Initial context
    new_pcb->context.cpu.segment.ds = GDT_USER_DATA_SEGMENT_SELECTOR | 0x3;
    new_pcb->context.cpu.segment.es = GDT_USER_DATA_SEGMENT_SELECTOR | 0x3;
    new_pcb->context.cpu.segment.fs = GDT_USER_DATA_SEGMENT_SELECTOR | 0x3;
    new_pcb->context.cpu.segment.gs = GDT_USER_DATA_SEGMENT_SELECTOR | 0x3;
    new_pcb->context.cpu.stack.esp  = PROCESS_STACK_TOP - 4;
    new_pcb->context.cpu.stack.ebp  = PROCESS_STACK_TOP - 4;
    new_pcb->context.eip    = (uint32_t) request.buf;
    new_pcb->context.eflags = CPU_EFLAGS_BASE_FLAG | CPU_EFLAGS_FLAG_INTERRUPT_ENABLE;
    new_pcb->context.page_directory_virtual_addr = new_dir;

    // Metadata
    uint32_t n = request.name_len < PROCESS_NAME_LENGTH_MAX - 1 ? request.name_len : PROCESS_NAME_LENGTH_MAX - 1;
    memcpy(new_pcb->metadata.name, request.name, n);
    new_pcb->metadata.state = PROCESS_STATE_READY;
    new_pcb->metadata.active = true;
    process_manager_state.active_process_count++;

exit_cleanup:
    paging_use_page_directory(prev_dir);          // always return to the previous address space
    if (retcode != PROCESS_CREATE_SUCCESS) {
        if (new_dir != NULL)
            paging_free_page_directory(new_dir);  // frees its frames and page tables too
        if (new_pcb != NULL)
            memset(new_pcb, 0, sizeof(*new_pcb));
    }
    return retcode;
}

struct ProcessControlBlock* process_get_current_running_pcb_pointer(void) {
    for (uint32_t i = 0; i < PROCESS_COUNT_MAX; i++)
        if (_process_list[i].metadata.active && _process_list[i].metadata.state == PROCESS_STATE_RUNNING)
            return &_process_list[i];
    return NULL;
}

bool process_destroy(uint32_t pid) {
    for (uint32_t i = 0; i < PROCESS_COUNT_MAX; i++) {
        struct ProcessControlBlock *pcb = &_process_list[i];
        if (!pcb->metadata.active || pcb->metadata.pid != pid)
            continue;

        // The directory being freed must not be the active one
        if (paging_get_current_page_directory_addr() == pcb->context.page_directory_virtual_addr)
            paging_use_page_directory(&_paging_kernel_page_directory);

        paging_free_page_directory(pcb->context.page_directory_virtual_addr);  // frees frames and page tables
        memset(pcb, 0, sizeof(*pcb));
        process_manager_state.active_process_count--;
        return true;
    }
    return false;
}
#include "header/process/process.h"
#include "header/memory/paging.h"
#include "header/stdlib/string.h"
#include "header/cpu/gdt.h"

#define PROCESS_USER_STACK_PAGE_VIRTUAL_ADDR 0x003FF000u
#define PROCESS_USER_STACK_TOP_VIRTUAL_ADDR  0x003FFFFCu

struct ProcessControlBlock _process_list[PROCESS_COUNT_MAX] = {0};

struct ProcessManagerState process_manager_state = {
    .active_process_count = 0,
    .last_pid             = 0,
};

static uint32_t ceil_div(uint32_t a, uint32_t b) {
    if (b == 0) {
        return 0;
    }
    return (a + b - 1u) / b;
}

static int32_t process_list_get_inactive_index(void) {
    for (int32_t i = 0; i < PROCESS_COUNT_MAX; i++) {
        if (_process_list[i].metadata.state == PROCESS_STATE_INACTIVE) {
            return i;
        }
    }
    return -1;
}

static uint32_t process_generate_new_pid(void) {
    for (uint32_t i = 0; i < PROCESS_COUNT_MAX; i++) {
        if (_process_list[i].metadata.state == PROCESS_STATE_INACTIVE) {
            process_manager_state.last_pid = i;
            return i;
        }
    }
    return process_manager_state.last_pid++;
}

struct ProcessControlBlock* process_get_current_running_pcb_pointer(void) {
    for (uint32_t i = 0; i < PROCESS_COUNT_MAX; i++) {
        if (_process_list[i].metadata.state == PROCESS_STATE_RUNNING) {
            return &_process_list[i];
        }
    }
    return NULL;
}

int32_t process_create_user_process(struct EXT2DriverRequest request) {
    int32_t retcode = PROCESS_CREATE_SUCCESS;
    if (process_manager_state.active_process_count >= PROCESS_COUNT_MAX) {
        retcode = PROCESS_CREATE_FAIL_MAX_PROCESS_EXCEEDED;
        goto exit_cleanup;
    }

    // Ensure entrypoint is not located at kernel's section at higher half
    if ((uint32_t)request.buf >= KERNEL_VIRTUAL_ADDRESS_BASE) {
        retcode = PROCESS_CREATE_FAIL_INVALID_ENTRYPOINT;
        goto exit_cleanup;
    }

    // Check whether memory is enough for the executable and additional frame for user stack
    uint32_t page_frame_count_needed = ceil_div(request.buffer_size + PAGE_FRAME_SIZE, PAGE_FRAME_SIZE);
    if (!paging_allocate_check(page_frame_count_needed * PAGE_FRAME_SIZE) ||
        page_frame_count_needed > PROCESS_PAGE_FRAME_COUNT_MAX) {
        retcode = PROCESS_CREATE_FAIL_NOT_ENOUGH_MEMORY;
        goto exit_cleanup;
    }

    // Process PCB
    int32_t p_index = process_list_get_inactive_index();
    if (p_index < 0) {
        retcode = PROCESS_CREATE_FAIL_MAX_PROCESS_EXCEEDED;
        goto exit_cleanup;
    }

    struct ProcessControlBlock *new_pcb = &(_process_list[p_index]);
    memset(new_pcb, 0, sizeof(*new_pcb));
    new_pcb->metadata.pid = process_generate_new_pid();

    // Copy executable name into kernel stack before switching CR3 address space
    char exec_name[PROCESS_NAME_LENGTH_MAX];
    memset(exec_name, 0, sizeof(exec_name));
    uint8_t copy_len = request.name_len;
    if (copy_len >= PROCESS_NAME_LENGTH_MAX) {
        copy_len = PROCESS_NAME_LENGTH_MAX - 1u;
    }
    if (request.name != NULL && copy_len > 0) {
        memcpy(exec_name, request.name, copy_len);
    }

    // 1. Create new virtual address space with page directory
    struct PageDirectory *old_page_dir = paging_get_current_page_directory_addr();
    struct PageDirectory *new_page_dir = paging_create_new_page_directory();
    if (new_page_dir == NULL) {
        memset(new_pcb, 0, sizeof(*new_pcb));
        retcode = PROCESS_CREATE_FAIL_NOT_ENOUGH_MEMORY;
        goto exit_cleanup;
    }

    // Temporarily switch to the new process virtual address space
    paging_use_page_directory(new_page_dir);

    // Allocate user page frames for executable code/data
    uint32_t exec_page_count = ceil_div(request.buffer_size, PAGE_FRAME_SIZE);
    if (exec_page_count == 0) {
        exec_page_count = 1;
    }

    bool alloc_ok = true;
    for (uint32_t i = 0; i < exec_page_count; i++) {
        void *vaddr = (void *)((uint32_t)request.buf + i * PAGE_FRAME_SIZE);
        if (!paging_allocate_user_page_frame(new_page_dir, vaddr)) {
            alloc_ok = false;
            break;
        }
        new_pcb->memory.virtual_addr_used[new_pcb->memory.page_frame_used_count++] = vaddr;
    }

    // Allocate user page frame for user call stack
    if (alloc_ok && (uint32_t)request.buf + exec_page_count * PAGE_FRAME_SIZE <= PROCESS_USER_STACK_PAGE_VIRTUAL_ADDR) {
        void *stack_vaddr = (void *)PROCESS_USER_STACK_PAGE_VIRTUAL_ADDR;
        if (!paging_allocate_user_page_frame(new_page_dir, stack_vaddr)) {
            alloc_ok = false;
        } else {
            new_pcb->memory.virtual_addr_used[new_pcb->memory.page_frame_used_count++] = stack_vaddr;
        }
    }

    if (!alloc_ok) {
        for (uint32_t i = 0; i < new_pcb->memory.page_frame_used_count; i++) {
            paging_free_user_page_frame(new_page_dir, new_pcb->memory.virtual_addr_used[i]);
        }
        paging_use_page_directory(old_page_dir);
        paging_free_page_directory(new_page_dir);
        memset(new_pcb, 0, sizeof(*new_pcb));
        retcode = PROCESS_CREATE_FAIL_NOT_ENOUGH_MEMORY;
        goto exit_cleanup;
    }

    // 2. Load executable from file system into new virtual memory
    struct EXT2DriverRequest kreq = request;
    kreq.name     = exec_name;
    kreq.name_len = copy_len;
    int8_t read_status = read(kreq);
    if (read_status != 0) {
        for (uint32_t i = 0; i < new_pcb->memory.page_frame_used_count; i++) {
            paging_free_user_page_frame(new_page_dir, new_pcb->memory.virtual_addr_used[i]);
        }
        paging_use_page_directory(old_page_dir);
        paging_free_page_directory(new_page_dir);
        memset(new_pcb, 0, sizeof(*new_pcb));
        retcode = PROCESS_CREATE_FAIL_FS_READ_FAILURE;
        goto exit_cleanup;
    }

    // 5. Restore previous virtual address space after loading executable
    paging_use_page_directory(old_page_dir);

    // 3. Setup initial Context for the new user process
    uint32_t user_data_segment = GDT_USER_DATA_SEGMENT_SELECTOR | 0x3u;
    new_pcb->context.cpu.stack.ebp              = PROCESS_USER_STACK_TOP_VIRTUAL_ADDR;
    new_pcb->context.cpu.stack.esp              = PROCESS_USER_STACK_TOP_VIRTUAL_ADDR;
    new_pcb->context.cpu.segment.gs             = user_data_segment;
    new_pcb->context.cpu.segment.fs             = user_data_segment;
    new_pcb->context.cpu.segment.es             = user_data_segment;
    new_pcb->context.cpu.segment.ds             = user_data_segment;
    new_pcb->context.eip                        = (uint32_t)request.buf;
    new_pcb->context.eflags                    |= CPU_EFLAGS_BASE_FLAG | CPU_EFLAGS_FLAG_INTERRUPT_ENABLE;
    new_pcb->context.page_directory_virtual_addr = new_page_dir;

    // 4. Record process metadata in PCB
    memcpy(new_pcb->metadata.process_name, exec_name, PROCESS_NAME_LENGTH_MAX);
    new_pcb->metadata.state = PROCESS_STATE_READY;
    process_manager_state.active_process_count++;

exit_cleanup:
    return retcode;
}

bool process_destroy(uint32_t pid) {
    for (uint32_t i = 0; i < PROCESS_COUNT_MAX; i++) {
        struct ProcessControlBlock *pcb = &_process_list[i];
        if (pcb->metadata.state == PROCESS_STATE_INACTIVE || pcb->metadata.pid != pid) {
            continue;
        }

        struct PageDirectory *old_page_dir = paging_get_current_page_directory_addr();
        struct PageDirectory *proc_page_dir = pcb->context.page_directory_virtual_addr;

        if (proc_page_dir != NULL) {
            paging_use_page_directory(proc_page_dir);
            for (uint32_t j = 0; j < pcb->memory.page_frame_used_count; j++) {
                paging_free_user_page_frame(proc_page_dir, pcb->memory.virtual_addr_used[j]);
            }
            if (old_page_dir != NULL && old_page_dir != proc_page_dir) {
                paging_use_page_directory(old_page_dir);
            } else {
                paging_use_page_directory(&_paging_kernel_page_directory);
            }
            paging_free_page_directory(proc_page_dir);
        }

        memset(pcb, 0, sizeof(*pcb));
        pcb->metadata.state = PROCESS_STATE_INACTIVE;
        if (process_manager_state.active_process_count > 0) {
            process_manager_state.active_process_count--;
        }
        return true;
    }
    return false;
}

static void append_str(char *buf, uint32_t *pos, uint32_t max_size, const char *str) {
    while (*str != '\0' && *pos + 1u < max_size) {
        buf[(*pos)++] = *str++;
    }
}

static void append_u32(char *buf, uint32_t *pos, uint32_t max_size, uint32_t val) {
    char tmp[12];
    uint32_t len = 0;
    if (val == 0) {
        tmp[len++] = '0';
    } else {
        while (val > 0 && len < sizeof(tmp)) {
            tmp[len++] = (char)('0' + (val % 10u));
            val /= 10u;
        }
    }
    while (len > 0 && *pos + 1u < max_size) {
        buf[(*pos)++] = tmp[--len];
    }
}

void process_get_list(char *buffer, uint32_t max_size) {
    if (buffer == NULL || max_size == 0) {
        return;
    }

    uint32_t pos = 0;
    for (uint32_t i = 0; i < PROCESS_COUNT_MAX; i++) {
        struct ProcessControlBlock *pcb = &_process_list[i];
        if (pcb->metadata.state == PROCESS_STATE_INACTIVE ||
            pcb->metadata.state == PROCESS_STATE_TERMINATED) {
            continue;
        }

        append_str(buffer, &pos, max_size, "PID ");
        append_u32(buffer, &pos, max_size, pcb->metadata.pid);
        append_str(buffer, &pos, max_size, ": ");
        append_str(buffer, &pos, max_size, pcb->metadata.process_name);
        if (pcb->metadata.state == PROCESS_STATE_RUNNING) {
            append_str(buffer, &pos, max_size, " [RUNNING]\n");
        } else {
            append_str(buffer, &pos, max_size, " [READY]\n");
        }
    }
    buffer[pos] = '\0';
}

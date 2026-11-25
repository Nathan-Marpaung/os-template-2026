#include "header/process/exec_args.h"
#include "header/process/process.h"
#include "header/memory/paging.h"
#include "header/filesystem/ext2.h"
#include "header/stdlib/string.h"

#define USER_SPACE_END        0xC0000000u
#define EXEC_ARGS_BUFFER_SIZE 0x40000      // sama dengan SYSCALL_EXEC

static bool is_user_pointer(const void *ptr) {
    return ptr != NULL && (uint32_t) ptr < USER_SPACE_END;
}

/* ------------------------------ exec dengan argumen ------------------------------ */

// PID selalu naik, jadi proses yang baru dibuat adalah proses aktif dengan PID terbesar
static struct ProcessControlBlock *find_newest_process(void) {
    struct ProcessControlBlock *newest = NULL;
    for (uint32_t i = 0; i < PROCESS_COUNT_MAX; i++) {
        struct ProcessControlBlock *pcb = &_process_list[i];
        if (pcb->metadata.active && (newest == NULL || pcb->metadata.pid > newest->metadata.pid))
            newest = pcb;
    }
    return newest;
}

int32_t process_create_with_args(const struct ExecArgsRequest *user_request) {
    if (!is_user_pointer(user_request))
        return -1;

    // 1. Salin request, nama, dan semua argumen ke memori kernel selagi
    //    page directory pemanggil (shell) masih aktif
    struct ExecArgsRequest req = *user_request;
    if (!is_user_pointer(req.name) || req.name_len == 0 || !is_user_pointer(req.argv) ||
        req.argc == 0 || req.argc > EXEC_ARGS_MAX_ARGC)
        return -1;

    char     name[PROCESS_NAME_LENGTH_MAX];
    uint32_t name_len = req.name_len < PROCESS_NAME_LENGTH_MAX - 1 ? req.name_len : PROCESS_NAME_LENGTH_MAX - 1;
    memcpy(name, req.name, name_len);
    name[name_len] = '\0';

    char     strings[EXEC_ARGS_MAX_BYTES];
    uint32_t offset[EXEC_ARGS_MAX_ARGC];
    uint32_t total = 0;
    for (uint32_t i = 0; i < req.argc; i++) {
        if (!is_user_pointer(req.argv[i]))
            return -1;
        uint32_t len = strlen(req.argv[i]) + 1;   // termasuk '\0'
        if (total + len > EXEC_ARGS_MAX_BYTES)
            return -1;
        memcpy(strings + total, req.argv[i], len);
        offset[i] = total;
        total    += len;
    }

    // 2. Buat proses dengan cara yang sama seperti SYSCALL_EXEC
    struct EXT2DriverRequest file = {
        .buf          = (void*) 0x0,
        .name         = name,
        .name_len     = (uint8_t) name_len,
        .parent_inode = req.parent_inode,
        .buffer_size  = EXEC_ARGS_BUFFER_SIZE,
        .is_directory = false,
    };
    int32_t retcode = process_create_user_process(file);
    if (retcode != PROCESS_CREATE_SUCCESS)
        return -retcode;

    struct ProcessControlBlock *pcb = find_newest_process();
    if (pcb == NULL)
        return -1;

    // 3. Masuk ke address space proses baru, tulis argumen di puncak stack-nya
    struct PageDirectory *prev_dir = paging_get_current_page_directory_addr();
    paging_use_page_directory(pcb->context.page_directory_virtual_addr);

    uint32_t sp = PROCESS_STACK_TOP;
    sp -= total;
    sp &= ~0x3u;                                    // rata 4 byte
    memcpy((void*) sp, strings, total);
    uint32_t string_base = sp;

    sp -= 4; *((uint32_t*) sp) = req.cwd_inode;     // tambahan priasolOS: cwd shell
    sp -= 4; *((uint32_t*) sp) = 0;                 // argv[argc] = NULL
    for (int32_t i = (int32_t) req.argc - 1; i >= 0; i--) {
        sp -= 4;
        *((uint32_t*) sp) = string_base + offset[i]; // argv[i]
    }
    sp -= 4; *((uint32_t*) sp) = req.argc;          // [esp] = argc

    paging_use_page_directory(prev_dir);

    // 4. Proses mulai dengan esp menunjuk ke argc (dipakai iret di context switch)
    pcb->context.cpu.stack.esp = sp;
    pcb->context.cpu.stack.ebp = sp;
    return (int32_t) pcb->metadata.pid;
}

bool process_is_alive(uint32_t pid) {
    for (uint32_t i = 0; i < PROCESS_COUNT_MAX; i++)
        if (_process_list[i].metadata.active && _process_list[i].metadata.pid == pid)
            return true;
    return false;
}

/* ------------------------------ shared memory ------------------------------ */

static char     shm_buffer[SHM_CHANNEL_COUNT][SHM_BUFFER_SIZE];
static uint32_t shm_length[SHM_CHANNEL_COUNT];

void shm_write(uint32_t channel, const char *src, uint32_t len) {
    if (channel >= SHM_CHANNEL_COUNT)
        return;
    if (len >= SHM_BUFFER_SIZE)
        len = SHM_BUFFER_SIZE - 1;                  // sisakan 1 byte untuk '\0'
    if (len > 0 && !is_user_pointer(src))
        return;
    if (len > 0)
        memcpy(shm_buffer[channel], src, len);
    shm_length[channel] = len;
}

uint32_t shm_read(uint32_t channel, char *dst) {
    if (channel >= SHM_CHANNEL_COUNT || !is_user_pointer(dst))
        return 0;
    uint32_t len = shm_length[channel];
    memcpy(dst, shm_buffer[channel], len);
    dst[len] = '\0';
    return len;
}
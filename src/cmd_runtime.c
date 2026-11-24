#include "header/stdlib/cmd_runtime.h"
#include "header/stdlib/syscall_nums.h"
#include "header/stdlib/string.h"

void syscall(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    __asm__ volatile(
        "int $0x30"
        : /* <Empty> */
        : "a"(eax), "b"(ebx), "c"(ecx), "d"(edx)
        : "memory"
    );
}

static struct ShellIO io;   // 8 KiB, di .bss supaya tidak memakan stack

struct ShellIO *cmd_begin(int argc, char **argv, uint32_t *cwd) {
    // Layout stack dari kernel: argv[argc] = NULL, argv[argc + 1] = cwd shell.
    // argc = 0 berarti dijalankan lewat "exec" biasa (tanpa argumen) -> pakai root.
    if (cwd != NULL)
        *cwd = (argc > 0) ? (uint32_t) argv[argc + 1] : 1;

    syscall(SYSCALL_SHM_READ, SHM_CHANNEL_INPUT, (uint32_t) io.input_buffer, 0);
    io.output_len = 0;
    return &io;
}

int cmd_end(struct ShellIO *io) {
    syscall(SYSCALL_SHM_WRITE, SHM_CHANNEL_OUTPUT, (uint32_t) io->output_buffer, io->output_len);
    return 0;
}

void cmd_write(struct ShellIO *io, const char *text) {
    int len = strlen(text);
    if (io->output_len + len >= MAX_IO_BUFFER)
        len = MAX_IO_BUFFER - 1 - io->output_len;
    memcpy(io->output_buffer + io->output_len, text, len);
    io->output_len += len;
}
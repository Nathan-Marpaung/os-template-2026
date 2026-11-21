#ifndef SYSCALL_NUMS_H
#define SYSCALL_NUMS_H

#include <stdint.h>

#define SYSCALL_READ        0
#define SYSCALL_READ_DIR    1
#define SYSCALL_WRITE       2
#define SYSCALL_DELETE      3
#define SYSCALL_GETCHAR     4
#define SYSCALL_PUTCHAR     5
#define SYSCALL_PUTS        6
#define SYSCALL_KEYBOARD    7
#define SYSCALL_CLEAR_SCR   8
#define SYSCALL_EXIT        10
#define SYSCALL_EXEC        11
#define SYSCALL_KILL        12
#define SYSCALL_PS          13

#define PROCESS_INFO_NAME_MAX 32

struct ProcessInfo {
    uint32_t pid;
    char     name[PROCESS_INFO_NAME_MAX];
    uint8_t  state;
};

#endif
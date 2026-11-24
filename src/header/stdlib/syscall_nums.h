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
#define SYSCALL_GET_TICKS   14
#define SYSCALL_KB_FOCUS    15
#define SYSCALL_KEY_MATRIX  16
#define SYSCALL_SPEAKER     17
#define SYSCALL_GET_TIME    20   // baru: ebx = pointer struct CMOSTime
#define SYSCALL_PUTS_AT     21   // baru: ebx = string, ecx = panjang, edx = (row << 16) | (col << 8) | color

#define PROCESS_INFO_NAME_MAX 32

struct ProcessInfo {
    uint32_t pid;
    char     name[PROCESS_INFO_NAME_MAX];
    uint8_t  state;
};

#endif
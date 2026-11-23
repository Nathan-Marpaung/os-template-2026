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
#define SYSCALL_GET_TIME    20
#define SYSCALL_PUTS_AT     21

#define SYSCALL_EXEC_ARGS     22
#define SYSCALL_PROCESS_ALIVE 23
#define SYSCALL_SHM_WRITE     24
#define SYSCALL_SHM_READ      25

#define EXEC_ARGS_MAX_ARGC    16
#define EXEC_ARGS_MAX_BYTES   512
#define SHM_BUFFER_SIZE       4096
#define SHM_CHANNEL_INPUT     0
#define SHM_CHANNEL_OUTPUT    1

#define PROCESS_INFO_NAME_MAX 32

struct ExecArgsRequest {
    char     *name;          
    uint8_t   name_len;
    uint32_t  parent_inode;  
    uint32_t  argc;
    char    **argv;          
    uint32_t  cwd_inode;    
};

struct ProcessInfo {
    uint32_t pid;
    char     name[PROCESS_INFO_NAME_MAX];
    uint8_t  state;
};

#endif